# NX2 Core Imports & Subsystem Architecture

This directory (`vendor/nx2_imports/`) preserves imported C++ architectural components, services, and shader translation layers for the 2nd-generation Nintendo Switch ("NX2") platform. These are reference sources, not compiled or linked into the SwitchRecomp C runtime. Their dependencies and integration adapters are not included as a complete buildable subsystem; importing them does not establish full NX2 game support.

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
  - Preserves kernel SVC dispatch and GPU memory attribute definitions from the imported implementation; these do not extend the live C runtime dispatch automatically.
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

## 3. Integration Status

The SwitchRecomp static recompiler translates guest AArch64 executable binaries (`.nro` or `exefs/`) into native C code. Its runtime remains a separate implementation:

- `runtime/svc_core.c` implements C HLE service dispatch. The imported branch adds `system_clock_context`, which serializes the host clock offset, steady time, and clock identifier. It does not connect the imported C++ services.
- `runtime/nvdrv.c` implements C NVDRV ioctl dispatch; it does not invoke these imported `nvhost_as_gpu` or `nvhost_gpu` classes.
- `runtime/shader.c` retains the existing C shader translator and Ampere support. It does not call the imported C++ `LowerToMaxwell()` implementation.

Connecting the reference sources requires explicit adapters, their upstream dependencies, build integration, and behavioral tests. Existing runtime improvements, regression tests, and binaries from `beta_nx2` are retained during the merge instead of replacing them with the older release baseline.

---

## 4. Build and Validation

Build the existing C runtime with the normal project command:

```bash
python3 switchrecomp.py --build-runtime --target linux --backend x11 --cc gcc
```

This command does not compile `vendor/nx2_imports/`. C++17/C++20 requirements in the imported source apply to future integration of those components, not to the current C runtime build.

Run the existing runtime regression checks with:

```bash
python3 scripts/test_runtime_ports.py
```

Passing these checks validates the covered C runtime behavior, not the completeness or buildability of the imported C++ subsystems.
