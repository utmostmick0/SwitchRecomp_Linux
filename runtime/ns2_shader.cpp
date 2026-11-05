/* SwitchRecomp, AGPL-3.0. Uses Eden's GPL-3.0-or-later Ampere frontend.
   Imported implementation and attribution remain in vendor/nx2_imports. */
#include "ns2_shader.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <exception>
#include "shader_recompiler/frontend/ampere/lower_bridge.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using namespace Shader::Ampere;
constexpr u32 Limit = 4096;
constexpr u64 BranchMask = u64{0xffffff} << 20;
u32 old_address(u32 i) { return 0x50 + i * 8 + (i + 1) / 3 * 8; }
u32 runtime_address(u32 i) { return 0x58 + i * 8 + i / 3 * 8; }
int fail(SwrNs2Shader* out, u32 offset, const char* why) {
    out->error_offset = offset;
    std::snprintf(out->error, sizeof out->error, "%s", why);
    return 0;
}
int lower(const u8* source, u32 size, bool compute, SwrNs2Shader* out) {
    const u32 header = compute ? 0 : 0x80;
    if (!source || size < header || size - header < sizeof(Instruction))
        return fail(out, 0, "short native shader");
    ProgramCaptureState capture;
    u32 bytes = 0;
    bool complete = false;
    for (u32 i = 0; i < Limit && i < (size - header) / sizeof(Instruction); ++i) {
        Instruction instruction{};
        std::memcpy(instruction.words.data(), source + header + i * 16, 16);
        const u32 op = instruction.Bits(0, 12);
        /* BRX needs native-PC table relocation in the downstream compiler;
           the C translator has no such contract. Never guess its targets. */
        if (op == 0x949 || op == 0x958)
            return fail(out, header + i * 16, "native indirect branch needs address-map support");
        auto state = capture.Observe(instruction, i);
        if (state == ProgramCaptureState::Result::InvalidTarget)
            return fail(out, header + i * 16, "native branch outside bounded program");
        bytes += 16;
        if (state == ProgramCaptureState::Result::Complete) { complete = true; break; }
    }
    if (!complete) return fail(out, header + bytes, "native program has no bounded terminator");
    auto lowered = LowerReviewedToMaxwell({source + header, bytes});
    if (!lowered.Ready()) {
        std::snprintf(out->error, sizeof out->error, "%.*s",
                      int(lowered.reason.size()), lowered.reason.data());
        out->error_offset = header + lowered.failed_native_byte_offset;
        return 0;
    }
    if (lowered.consumed_native_bytes != bytes)
        return fail(out, header, "native capture and lowering lengths disagree");
    /* Eden's environment starts at 0x50 with absolute 32-byte scheduling
       boundaries. This runtime starts at 0x58, with groups relative to 0x50.
       Repack and relocate every direct control-flow word, not just the header. */
    const auto& instructions = lowered.maxwell_instructions;
    if (runtime_address(u32(instructions.size() - 1)) / 8 >= 16384)
        return fail(out, header, "lowered program exceeds C translator instruction limit");
    std::vector<u64> image((runtime_address(u32(instructions.size() - 1)) + 8) / 8);
    if (!compute) std::memcpy(image.data(), source, 0x50);
    std::vector<u32> positions;
    positions.reserve(instructions.size());
    for (u32 i = 0; i < instructions.size(); ++i) positions.push_back(old_address(i));
    for (u32 i = 0; i < instructions.size(); ++i) {
        u64 word = instructions[i];
        const u32 op = u32(word >> 48);
        if (op == 0xe240 || op == 0xe260 || op == 0xe290 || op == 0xe2a0) {
            s32 relative = s32(u32((word >> 20) & 0xffffff) << 8) >> 8;
            s64 target = s64(old_address(i)) + 8 + relative;
            if (target < 0 || target > UINT32_MAX)
                return fail(out, header, "invalid lowered branch address");
            auto it = std::lower_bound(positions.begin(), positions.end(), u32(target));
            if (it == positions.end() || *it != target)
                return fail(out, header, "lowered branch does not target an instruction");
            s64 distance = s64(runtime_address(u32(it - positions.begin()))) - runtime_address(i) - 8;
            if (distance < -0x800000 || distance > 0x7fffff)
                return fail(out, header, "runtime branch displacement overflow");
            word = (word & ~BranchMask) | ((u64(distance) & 0xffffff) << 20);
        }
        image[runtime_address(i) / 8] = word;
    }
    out->size = u32(image.size() * 8);
    out->code = static_cast<u8*>(std::malloc(out->size));
    if (!out->code) { out->size = 0; return fail(out, header, "native shader allocation failed"); }
    std::memcpy(out->code, image.data(), out->size);
    out->consumed = header + bytes;
    return 1;
}
}

extern "C" int swr_ns2_shader_lower(const uint8_t* source, uint32_t size, int compute,
                                    SwrNs2Shader* out) {
    if (!out) return 0;
    *out = {};
    if (compute != 0 && compute != 1) return fail(out, 0, "invalid shader layout");
    try { return lower(source, size, compute != 0, out); }
    catch (const std::exception& e) { return fail(out, 0, e.what()); }
    catch (...) { return fail(out, 0, "native shader translation exception"); }
}

extern "C" int swr_ns2_shader_capture(uint64_t address, int compute,
                                      SwrNs2ShaderRead read, void* context,
                                      SwrNs2Shader* out) {
    if (!out) return 0;
    *out = {};
    if (!read || (compute != 0 && compute != 1) || address > UINT64_MAX - 0x80 - Limit * 16)
        return fail(out, 0, "invalid native shader capture");
    try {
        const u32 header = compute ? 0 : 0x80;
        std::vector<u8> source(header);
        source.reserve(header + Limit * 16);
        if (header && !read(context, address, source.data(), header))
            return fail(out, 0, "unmapped native shader header");
        ProgramCaptureState capture;
        for (u32 i = 0; i < Limit; ++i) {
            Instruction instruction{};
            const u32 offset = header + i * 16;
            if (!read(context, address + offset, instruction.words.data(), 16))
                return fail(out, offset, "unmapped native shader instruction");
            const auto* bytes = reinterpret_cast<const u8*>(instruction.words.data());
            source.insert(source.end(), bytes, bytes + 16);
            auto state = capture.Observe(instruction, i);
            if (state == ProgramCaptureState::Result::InvalidTarget)
                return fail(out, offset, "native branch outside bounded program");
            if (state == ProgramCaptureState::Result::Complete)
                return lower(source.data(), u32(source.size()), compute != 0, out);
        }
        return fail(out, u32(source.size()), "native program has no bounded terminator");
    } catch (const std::exception& e) { return fail(out, 0, e.what()); }
    catch (...) { return fail(out, 0, "native shader capture exception"); }
}
