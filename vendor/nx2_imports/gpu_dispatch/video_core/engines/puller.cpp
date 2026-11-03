// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/assert.h"
#include "common/scope_exit.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/control/channel_state.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/engines/puller.h"
#include <span>
#include <memory>
#include <atomic>
#include <algorithm>
#include <limits>
#include <cstdio>
#include "core/hle/service/nvdrv/devices/native_gpu_channel_registry.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/native_qmd.h"

                        std::fprintf(stderr,
                            "EDEN_NS2_COMPUTE_PREPARED count=%u program=%llx grid=%u,%u,%u block=%u,%u,%u shader_bridge=off backend=missing\n",
                            n, static_cast<unsigned long long>(launch->program_address),
                            launch->grid[0], launch->grid[1], launch->grid[2],
                            launch->block[0], launch->block[1], launch->block[2]);

                    } else {
                        std::fprintf(stderr,
                            "EDEN_NS2_COMPUTE_PREPARE_REJECTED count=%u backend=missing\n", n);
                    }
                }
#else
                (void)mapped;
                (void)compute_ticket;
                (void)channel_ticket;
#endif
                return false;
            });
    }
    if (!error.empty()) {
        LOG_ERROR(HW_GPU, "Native QMD3 rejected: {}", error);
    }
}

/// Calls a GPU engine method.
void Puller::CallEngineMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    const EngineID engine = bound_engines[method_call.subchannel];
    if (engine == static_cast<EngineID>(0xc797)) {
        CallNative3D(dma_pusher,method_call.method,
                     std::span<const u32>{&method_call.argument,1},
                     method_call.IsLastCall());
        return;
    }
    if (engine == static_cast<EngineID>(0xc7c0)) {
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            UNIMPLEMENTED_MSG("Ampere compute command on non-native channel");
            return;
        }
        CallNativeCompute(dma_pusher, method_call.method,
                          std::span<const u32>{&method_call.argument, 1},
                          method_call.IsLastCall(), method_call.subchannel);
        return;
    }
    if (engine == static_cast<EngineID>(0xc7b5)) {
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            dma_pusher.channel_state.payload->maxwell_dma.CallMethod(
                dma_pusher.system, method_call.method, method_call.argument,
                method_call.IsLastCall());
        } else {
            UNIMPLEMENTED_MSG("Ampere DMA method on a non-native GPU channel");
        }
        return;
    }
    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.channel_state.payload->fermi_2d.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.channel_state.payload->maxwell_3d.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.channel_state.payload->kepler_compute.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.channel_state.payload->maxwell_dma.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.channel_state.payload->kepler_memory.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.channel_state.payload->nv01_timer.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU engine multivalue method.
void Puller::CallEngineMultiMethod(DmaPusher& dma_pusher, u32 method, u32 subchannel, const u32* base_start, u32 amount, u32 methods_pending) {
    const EngineID engine = bound_engines[subchannel];
    if (engine == static_cast<EngineID>(0xc797)) {
        CallNative3D(dma_pusher,method,
                     std::span<const u32>{base_start,amount},
                     amount==methods_pending);
        return;
    }
    if (engine == static_cast<EngineID>(0xc7c0)) {
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            UNIMPLEMENTED_MSG("Ampere compute multimethod on non-native channel");
            return;
        }
        if ((amount && !base_start) || amount > 4096) {
            UNIMPLEMENTED_MSG("Ampere compute multiword payload invalid");
            return;
        }
        CallNativeCompute(dma_pusher, method,
                          std::span<const u32>{base_start, amount},
                          amount == methods_pending, subchannel);
        return;
    }
    if (engine == static_cast<EngineID>(0xc7b5)) {
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            dma_pusher.channel_state.payload->maxwell_dma.CallMultiMethod(
                dma_pusher.system, method, base_start, amount, methods_pending);
        } else {
            UNIMPLEMENTED_MSG("Ampere DMA multimethod on a non-native GPU channel");
        }
        return;
    }
    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.channel_state.payload->fermi_2d.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.channel_state.payload->maxwell_3d.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.channel_state.payload->kepler_compute.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.channel_state.payload->maxwell_dma.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.channel_state.payload->kepler_memory.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.channel_state.payload->nv01_timer.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU method.
void Puller::CallMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method_call.method, method_call.subchannel);
    ASSERT(method_call.subchannel < bound_engines.size());

    if (ExecuteMethodOnEngine(dma_pusher, method_call.method)) {
        CallEngineMethod(dma_pusher, method_call);
    } else {
        CallPullerMethod(dma_pusher, method_call);
    }
}

/// Calls a GPU multivalue method.
void Puller::CallMultiMethod(DmaPusher& dma_pusher, u32 method, u32 subchannel, const u32* base_start, u32 amount, u32 methods_pending) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method, subchannel);
    ASSERT(subchannel < bound_engines.size());
    if (ExecuteMethodOnEngine(dma_pusher, method)) {
        CallEngineMultiMethod(dma_pusher, method, subchannel, base_start, amount, methods_pending);
    } else {
        for (u32 i = 0; i < amount; i++) {
            CallPullerMethod(dma_pusher, MethodCall{
                method,
                base_start[i],
                subchannel,
                methods_pending - i,
            });
        }
    }
}

/// Determines where the method should be executed.
[[nodiscard]] bool Puller::ExecuteMethodOnEngine(DmaPusher& dma_pusher, u32 method) {
    const auto buffer_method = BufferMethods(method);
    return buffer_method >= BufferMethods::NonPullerMethods;
}

} // namespace Tegra::Engines
