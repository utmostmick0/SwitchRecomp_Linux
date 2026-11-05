# SwitchRecomp Bananza and Eden Compatibility: Technical Progress

> Historical investigation snapshot. For the current merged runtime, validation
> results, NS2 integration boundaries and all supplied reference mappings, see
> [NS2 technical integration](docs/NS2_TECHNICAL_INTEGRATION.md) and
> [the session handoff](SESSION_HANDOFF.md).

## Purpose and current status

This document records the work completed so far to improve AArch64 instruction
coverage, Switch 2 shader compatibility, and Donkey Kong Bananza startup in
SwitchRecomp. It also distinguishes verified changes from experiments and open
issues.

The current target is Donkey Kong Bananza (`040074A01BF12000`). The game has
advanced through several startup failures, but it has **not been confirmed to
reach rendering in SwitchRecomp**. The latest relevant log still ends in an
SDK abort during `TryQueryDonatedTransferMemoryUsage`. Eden can get farther
into the game, but its captured logs also contain shader, storage-buffer, and
video-interface errors; these have not been proven to share the same cause.

At the last repository check, the branch was `beta_nx2` at commit `02ad1fc`
(`Add AArch64 atomics and Ampere shader lowering`). The worktree contains
additional uncommitted changes. No commit was made for the work described
below the current branch tip.

## Compatibility architecture

SwitchRecomp has separate static-recompilation and runtime responsibilities:

- `a64.py` translates AArch64 instructions into generated C for both compiled
  functions and interpreter paths. Changing its decoder requires regenerating
  and rebuilding the game's code module.
- `runtime/cpu.h` and `runtime/cpu_helpers.c` provide CPU state and helper
  operations used by generated code.
- `runtime/shader.c` translates the Maxwell-format shader stream to GLSL.
  Shader translator changes require rebuilding and installing the runtime;
  they do not normally require regenerating the game's static code module.
- `codegen.py` can replace selected guest functions with generated stubs.
  Changes to those stubs require regenerating and rebuilding the game module.
- `runtime/kernel.c`, `runtime/svc_core.c`, and other runtime files implement
  operating-system services and HLE behavior. Those changes require a runtime
  rebuild and deployment to the game executable.

These are separate artifacts. Updating only the runtime does not make new
AArch64 decoder templates appear in an already-built `_code.so`, and rebuilding
the game module does not install a new runtime.

## AArch64 instruction support

### PAC and authenticated instructions

Support was added or retained in `a64.py` for pointer-authentication-related
instructions, including authenticated returns and branches, authenticated
loads, and `XPACI`. Instructions that are hints or do not require a distinct
host operation are treated as no-ops where appropriate.

### CAS, LSE atomics, and LDAPR

The initial unsupported Bananza instruction `0x88e87c14` was decoded as
`CASA w8, w20, [x0]`. This prompted additions for scalar compare-and-swap and
the scalar LSE atomic families, plus acquire-load support for LDAPR byte,
halfword, word, and doubleword forms.

Runtime helpers were added for value-returning compare-and-swap and atomic
read-modify-write operations. The helpers cover the operation sizes and
semantics needed by the decoder; the non-TCC implementation uses compiler
atomics, while TCC uses host-specific atomic sequences. Decoder ordering matters:
the more-specific LDAPR encodings must precede the broader LSE pattern.

### Eden AArch64 parity batch

An additional implementation batch added CASP, authenticated loads and
branches, pointer-auth operations, and memory-tag instructions (MTE) to the
decoder/generator. A 128-bit CAS helper uses `cmpxchg16b` on x86-64.

**Portability caveat:** the non-x86 fallback for the 128-bit CAS helper is
implemented as two-word atomic operations and does not guarantee atomicity of
the pair. It should not be treated as a fully portable CASP implementation.

### CPU validation

The following targeted checks passed during the session:

- `scripts/test_eden_a64.py`: static/interpreter A64 generation coverage for the
  Eden parity additions.
- `scripts/test_lse_decode.py`: assembled CAS/LSE/LDAPR encodings through static
  and interpreter generation.
- `scripts/test_lse_atomics.c`: CAS results, LSE operation behavior, and
  concurrent increments.
- Python syntax checks for the modified decoder and tests.
- Generated C compile checks for representative instruction templates.

An earlier broad `scripts/test_runtime_ports.py` run passed several groups but
stopped at a pre-existing service-port test compilation failure referencing
`system_clock_context`; that failure was unrelated to the atomic work.

## Shader compatibility and Eden investigation

### Existing pipeline and scope

SwitchRecomp's shader translator consumes Maxwell GM20B SASS and emits GLSL.
Eden's Switch 2 path includes Ampere-to-Maxwell lowering built on Eden-specific
IR. That lowering cannot be copied wholesale into SwitchRecomp's independent
translator; individual instruction semantics must be ported and tested.

Eden investigation identified logged Ampere operations such as MUFU, indexed
constant loads, F2I, HSETP2, and uniform vote. Eden also contains broader
Ampere lowering for operations such as IADD3 carry handling, TMML, and HMMA.
Full instruction and shader parity with Eden has **not** been achieved.

### HSETP2

A narrow Ampere immediate HSETP2 conversion was added in `runtime/shader.c` and
declared in `runtime/shader.h`. `scripts/test_shader_ampere.c` includes helper
checks and exact captured instruction fixtures. These tests passed.

This helper is not a complete Ampere instruction-stream decoder or a general
Ampere-to-Maxwell translation pipeline. Earlier checkpointed work explicitly
noted that the helper was not wired into runtime stream detection; later work
added immediate-form lowering support, but end-to-end handling of arbitrary
Ampere streams remains open.

### Guest shader captures and CAL/RET

Eden guest shader dumps (`.ash`) and backend SPIR-V (`.spv`) were collected for
Bananza. The `.spv` files validate as Vulkan SPIR-V, but they are backend
output, not the original guest instruction stream. The `.ash` files are binary
and 16-byte aligned; the first opcode scan was exploratory only. Its raw
low-bit counts did not establish instruction boundaries or a complete file
format. The correct way to interpret and cross-reference captures is to verify
the dump layout and strip stage suffixes (`_vs`, `_fs`, `_cs`) when matching
shader stems.

Subsequent capture analysis found 12 shaders containing Maxwell-style `CAL`
subroutine calls and `RET` returns. SwitchRecomp previously treated `CAL` as
unsupported and its length/hash traversal did not include called subroutines.
`runtime/shader.c` was updated to:

- Include `CAL` targets and relevant return code when determining shader
  extent/hash.
- Push a return instruction index for `CAL`, jump to its target, and restore
  control flow on `RET`.
- Use dynamic control-flow translation for shaders containing calls rather
  than applying the existing static SSY/PBK/PCNT stack analysis, which does
  not model call edges.
- Bound generated call-stack storage and handle overflow without writing past
  the stack.

All 12 captured CAL shaders in the checked subset translated with zero
unsupported instructions. A broader scan found 30 shaders with untranslated
instructions among 593 files eligible for the existing translator. This is a
corpus result, not proof that all game-critical shaders work.

### Shader validation and tracing

Passed targeted tests include:

- `scripts/test_shader_calls.c`: synthetic CAL/RET call-stack and hash-extent
  regression.
- `scripts/test_shader_ampere.c`: HSETP2 lowering and captured fixtures.
- `scripts/test_shader_dimensions.c`: existing shader-dimensions regression.
- `git diff --check`.

SwitchRecomp's F5 tracing can record draw/compute activity, shaders/GLSL,
constant buffers, textures, and render-target snapshots once the runtime
renders a frame. It cannot diagnose Bananza's current pre-render SDK abort.

## Bananza startup and runtime fixes

Each startup change was narrow and driven by a new `games/base.log`.

### Applet proxy and allocation alignment

- `runtime/svc_core.c`: `appletOE` command 10 now returns an application proxy
  rather than a success-shaped response without the expected object.
- `runtime/kernel.c`: SVC GetInfo type 33 returns `0x1000` (4 KiB allocation
  alignment), which allowed the game to pass `nn::ro::ProtectRelro`.

The updated logs advanced beyond these failures, indicating the relevant
runtime paths were reached.

### Experimental graphics-startup bypasses

Opt-in generated stubs were added to `codegen.py`, exposed through
`switchrecomp.py`, tested in `scripts/test_codegen_remote_memory.py`, and
documented in `README_LINUX.txt`. The bypasses target specific SDK symbols:

- `nn::gsv::BootWithRemoteMemory` and its callback overload.
- `nn::syncpt::sdk::Initialize`.
- `nn::gs::sdk::BootNativeMemoryManagement`.
- `nn::gs::sdk::BootGraphicsFrameManagement`.
- `nn::gpu::sdk::TryQueryDonatedTransferMemoryUsage`.

The relevant CLI switches are:

- `--skip-switch2-remote-memory`
- `--skip-switch2-syncpoint-init`
- `--skip-switch2-native-memory-init`
- `--skip-switch2-frame-management-init`
- `--skip-switch2-donated-transfer-memory-query`

These are experimental compatibility bypasses, not implementations of the
Switch 2 graphics services. Skipping initialization may let startup proceed
but can leave graphics functionality unavailable. The donated-memory query
stub returns zero in `x0` and returns through the guest link register; the
correct behavior and output-pointer contract still need validation.

### Current donated-memory query failure

The latest `games/base.log` records runtime
`v196-code-module-path`, loads `games/base_code.so` with 1,203,372 entries, and
reports the guest code range `0x8000000–0xABE8A70`. It still ends with
`svcBreak` at about 0.66 seconds. The guest backtrace includes
`TryQueryDonatedTransferMemoryUsage` at `0xA842350`, called from
`BootGraphicsSystem` at `0xA8519F8`; no GPU frame submission is shown before
the failure.

The generated `F_A842350` is a stub in `games/base_build/code_248.c`. A
temporary module probe also found the module entry for `0xA842350` pointing to
that stub. The caller reaches a PLT/vtable helper (`F_ABD74F0`) that loads a
guest function pointer and invokes it through `CALLDYN`/`swr_lookup`.
Nevertheless, the crash stack contains the query frame. The available evidence
does not yet explain whether the original body is being reached through an
unexpected dispatch path, another thread/module, or a stale/overlapping call
path. The guest instruction dump reflects guest memory and alone does not prove
which host function ran.

### Dispatch-trace diagnostic in progress

To resolve the discrepancy, a configurable trace was added:

- Set `SWR_TRACE_GUEST_PC` to a guest address (for example
  `0xA842350`) to trace matching `swr_lookup` dispatches.
- The runtime callback logs the guest PC, LR, host function pointer,
  interpreter status, and thread pointer, capped at 64 hits.
- The target is stored in the unused `CPU.user[7]` slot for each new guest
  thread.

The runtime rebuilt successfully after this trace change. The new runtime has
not yet been copied over `games/base`. Rebuilding the generated game module was
started so its inline `swr_lookup` would include the trace hook, but was stopped
after 40 of 281 generated C files; the trace-enabled game module was not linked
or installed. Therefore the trace is not yet ready for an in-game run.

## Eden comparison and unresolved failures

The locally inspected Eden fork was at commit `1dd40d86df`. Its working-tree
edit in `src/video_core/texture_cache/util.cpp` was deliberately left
untouched. Eden's AArch64 support covers additional ARMv8.1–8.5 atomics,
pointer authentication, and memory tagging. Its shader implementation includes
an Ampere-to-Maxwell lowering pipeline and cannot be transplanted directly.
SwitchRecomp has only partial CPU and shader parity; the phrase “match all
instructions/shaders from Eden” remains a longer-term parity effort.

Eden logs showed Bananza reaching in-game graphics in that fork, alongside
shader/Vulkan validation messages. Later logs also showed compute storage-buffer
index-limit errors, an unimplemented `nx2:vi2` command, and a userspace panic
or SIGSEGV. These logs do not establish that the SwitchRecomp crash or Eden
crash has one common cause.

## Important files and artifacts

- `a64.py` — AArch64 decoder and generated instruction templates.
- `runtime/cpu.h`, `runtime/cpu_helpers.c` — CPU state, atomic helpers, and
  guest-to-host dispatch helpers.
- `runtime/shader.c`, `runtime/shader.h` — Maxwell shader translator, CAL/RET
  work, and limited HSETP2 lowering.
- `runtime/gpu_gl.c` — graphics/compute shader ingestion and F5 capture path.
- `runtime/kernel.c`, `runtime/svc_core.c` — HLE/SVC startup behavior.
- `codegen.py`, `switchrecomp.py` — opt-in Bananza SDK stubs and CLI wiring.
- `scripts/test_eden_a64.py`, `scripts/test_lse_decode.py`,
  `scripts/test_lse_atomics.c` — A64 atomic and Eden parity tests.
- `scripts/test_shader_ampere.c`, `scripts/test_shader_calls.c`,
  `scripts/test_shader_dimensions.c` — shader lowering and control-flow tests.
- `scripts/test_codegen_remote_memory.py` — opt-in SDK stub tests.
- `games/base.log`, `games/base_build/`, `games/base_code.so` — current Bananza
  log, generated code, and game module.
- `bin/runtime_linux_x11`, `games/base` — built runtime and installed game
  executable copies. These are generated binaries and should not be reverted
  casually.

## Remaining work

1. Finish and run the dispatch trace for `0xA842350`; determine the actual
   function selected by the running guest thread.
2. Validate the donated-transfer query's return and output-pointer contract
   before treating its zero-return stub as correct.
3. Rebuild/install the runtime and game module only after the trace-enabled
   module is complete; run Bananza and inspect the resulting log.
4. Continue implementing Ampere shader lowering one verified opcode family at
   a time, then add reliable stream detection and integrate it into both
   graphics and compute shader paths.
5. Continue Eden CPU/shader parity with focused semantic tests; document
   unsupported cases instead of claiming full parity.
6. Preserve existing dirty worktree changes. The current uncommitted changes
   include the trace instrumentation and previously completed CPU/shader,
   startup, test, and documentation updates.
