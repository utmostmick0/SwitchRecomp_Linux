# NX2 Core Imports & Subsystem Architecture

This directory (`vendor/nx2_imports/`) contains the core architectural components, services, and shader translation layers supporting the 2nd-generation Nintendo Switch ("NX2") platform within the SwitchRecomp runtime.

---

## 1. Subsystem Architecture Overview

The NX2 subsystem is organized into several modular domains:

- **AM & Process Lifecycle (`am/`, `self_controller/`, `process/`)**:
  - Implements HLE handlers for application proxy, application functions, and graphics scaling parameter interfaces.
  - Controls process creation, memory layouts, and process capability admission.

- **Graphics Supervisor & GPU Session (`graphics_supervisor/`, `gpu_session/`, `hle/`)**:
  - **`IGraphicsSupervisor`**: Manages the high-level graphics lifecycle, bootstrap negotiations, native memory pools, and display sessions (`IVi2Session`).
  - **`IGpuSession` & `IGpuWorker`**: Implements the IPC transport for GPU address space partitioning, worker leasing, and asynchronous command/event notifications.

- **Ampere Shader Recompiler & Lowering Bridge (`ampere/`, `shader_resource/`)**:
  - **Ampere ISA Decoder**: Decodes 128-bit native Ampere shader instructions (`instruction.h`, `ampere_isa_table.h`).
  - **Lowering Engine (`lower_to_maxwell.h`)**: Maps Ampere instructions into semantically equivalent Maxwell instructions, handling branch relocations, barrier scopes, uniform bank mapping, and register allocation.
  - **Environment Adapter (`maxwell_environment.h`, `maxwell_image.h`)**: Wraps compiled shader images to supply Maxwell-compatible instruction streams to downstream compiler backends (such as SPIR-V Cross or OpenGL/Vulkan GLSL).

- **NVHost & GMMU Devices (`nvdrv/`, `nvdrv_as/`, `nvdrv_gpu/`, `gpu_dispatch/`)**:
  - **`nvhost_as_gpu`**: Emulates the GPU Virtual Memory Address Space (GMMU), managing page allocations, mapping tables, and sparse remapping entries.
  - **`nvhost_gpu`**: Manages GPU channels, GPFIFO execution queues, and hardware fence tracking.
  - **`puller` & `macro_turing`**: Pulls command pushbuffers and interprets GPU macro state machines for compute and 3D pipelines.

- **Kernel & GPU Memory Access SVCs (`kernel/`, `gpu_access_svc/`, `cpu6/`)**:
  - Handles NX2 SVC extensions (such as SVC 0x80 `ManageNamedPort` / GPU memory attribute toggles).
  - Emulates ARM64 exclusive monitors (`exclusive_monitor.h`) and hardware configuration properties.

- **Audio Subsystem (`audio/`)**:
  - Provides NX2 audio user client shims and StreamPlay protocols for multi-stream audio routing and PCM output.

---

## 2. Virtual Memory Translation and MMU Mappings

### GMMU Layout & Address Space Partitions
NX2 utilizes an expanded 40-bit or 48-bit GPU virtual address space managed through `nvhost_as_gpu`. The address space is partitioned into:
- **Low Space / System Reserved**: Mapped for fixed GPU descriptors and firmware structures.
- **User Space**: Dynamic allocations created via `ALLOC_SPACE` or `MAP_BUFFER_EX`.
- **Sparse Virtual Address Regions**: High-address range used for virtual texture and buffer tiles that may be bound to physical memory on demand.

### Sparse Page Remapping (`NativeSparseRemap`)
Sparse mappings allow binding small (4KB/64KB) or large (2MB) guest memory pages to GPU VA windows without allocating linear physical backing immediately:
1. `nvhost_as_gpu::MapNativeSparseBatch` accepts a batch of remap descriptors.
2. The batch validates ownership against process-held NVMap allocations.
3. Pins are acquired atomically and intermediate GMMU page table updates are applied sequentially.
4. Memory regions that are unmapped or rebound safely flush buffer cache regions to prevent stale host cache reads.

---

## 3. Interfacing with SwitchRecomp

The SwitchRecomp static recompiler translates guest AArch64 executable binaries (`.nro` or `exefs/`) directly into native C code. The NX2 core components interface with the recompiled binary and runtime via:

1. **SVC Dispatching (`runtime/svc_core.c`)**:
   - Guest system call invocations emitted by the recompiler route to the C HLE runtime, which calls into the corresponding kernel and graphics supervisor shims.
2. **NVDRV Ioctl Dispatching (`runtime/nvdrv.c`)**:
   - Channel commands and address space configurations from guest user mode route through `nvhost_as_gpu` and `nvhost_gpu` device handlers.
3. **Shader JIT Lowering Bridge (`runtime/shader.c`)**:
   - When shaders are encountered at runtime or during offline compilation, Ampere instructions are captured, lowered through `LowerToMaxwell()`, and fed into the buffer cache and pipeline compiler.

---

## 4. Platform-Specific Build Flags and Requirements

Building the SwitchRecomp runtime with NX2 support requires modern C++17/C++20 toolchains and native development libraries:

### Host Dependencies
- **Linux**:
  - GCC 11+ or Clang 14+ supporting C++20.
  - Development headers: `libx11-dev`, `libgl-dev`, `libvulkan-dev`, `libegl-dev`, `libgl1-mesa-dev`, `libglx-dev`.

### Runtime Build Command
To compile the runtime with the X11 backend:
```bash
python3 switchrecomp.py --build-runtime --target linux --backend x11 --cc gcc
```

### Compiler Definitions
- `-std=c++20`: Required for standard library concepts, `std::span`, and modern RAII constructs.
- `-march=native`: (Optional) Optimizes atomic and vector operations for the host architecture.
- `-static-libgcc -static-libstdc++`: Ensures portability across Linux distributions by statically linking the C++ standard library.
