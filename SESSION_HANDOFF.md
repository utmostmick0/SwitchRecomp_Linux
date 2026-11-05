# Runtime handoff

Updated: 2026-10-29. This file is the current status summary; dated sections in older reports are historical.

## Build and launch

- Workspace: `/mnt/Media2/SwitchRecomp_Linux`.
- Runtime: `v195-audio-completion`, [Linux X11 binary](bin/runtime_linux_x11).
- SHA256: `ee7285add01fdbb74b613e422385cbfbef8f4e37bda0964c0465f5aa99ef0d93`.
- Matching executables are staged for Balatro, Belmonts Curse, Bubble Bobble, FFRS, SMW, smo, and XC2. Launch through SwitchRecomp normally.
- Rollback executables remain under `games/*.pre-*`. No saves, regular logs, game assets, generated game code, or rollbacks were removed during cleanup.

Build:

```sh
python3 switchrecomp.py --build-runtime --target linux --backend x11 --cc gcc
```

Building updates the central binary; existing per-game executables also need copying/staging after later rebuilds.

## Current tested behavior

| Game | Evidence and limits |
| --- | --- |
| SMO | Audio completion smoke test: 508 frames / 3,725 draws at 11.66 seconds. Former zero-draw startup stall cleared in that test. Full gameplay/audio correctness unverified. |
| XC2 | Same smoke test: 259 frames at 11.63 seconds, texture-type mismatches=0. Earlier shader-specific test reached 413 frames. Visual correctness needs gameplay testing. |
| Belmonts Curse | Earlier native-clock test: 1,097 frames at 21.67 seconds without the prior clock exception. Not revalidated in the final audio build. |
| Balatro / Bubble Bobble | Earlier isolated audit tests kept presenting, with no fatal error recorded. Not revalidated in the final audio build. |
| SMW | Latest examined regular log reached 1,095 frames and ended with user window close, not a recorded crash. Earlier isolated runs stalled; this remains inconsistent. |
| FFRS | Earlier isolated test: no draws/submissions after 30 seconds. Startup stall remains unresolved. |

Primary recent evidence: [SMO](games/smo.audio-timeline-smoke.log), [XC2](games/XC2.audio-timeline-smoke.log). Tests used isolated save/config/cache folders; regular launch logs were preserved. Functional smoke tests preceded only the final runtime version-marker update.

## Implementation map

- [Audio RequestUpdate](runtime/main.c): renderer-info counter advances by rendered 5 ms quanta and holds while stopped. Successful running updates finish output payloads before [completion notification](runtime/svc_core.c). REV3 does not get a fabricated renderer-info section. Voice samples track actual mixer consumption.
- [Shader translator](runtime/shader.c) and [descriptor specialization/cache](runtime/gpu_gl.c): TIC type 2 volumes select compatible fragment TEX/TLD/TEXS/TLDS variants with sampler3D and vec3/ivec3 coordinates. Stable slot identities and explicit variant metadata prevent stale 2D cache reuse. Gather, shadow, array, mixed incompatible uses, and packed-offset forms are excluded.
- [Vulkan X11 output](runtime/vk_output_x11.c): OpenGL rendering plus CPU readback/staging upload into an Xlib swapchain, not native Vulkan game rendering. Readback explicitly selects the default read framebuffer, unbinds pack buffers, validates completeness, and restores state. Creation/resize checks share a surface-clamped extent calculation. Guest resolution is scaled to the host window.
- [nvhost synchronization](runtime/nvdrv.c): event creation is synchronized; async waits arm a retained event even before explicit registration. Commands c004001f and c010001e are event registration/wait, not GPU capability structs.
- [Semaphore reports](runtime/gpu_engines.c): writes to 0x500000000 yield after immediate writeback; deferred [compute completion](runtime/gpu_gl.c) yields after publishing its held write. This alone did not clear SMO's stall.
- [SleepThread](runtime/kernel.c): thread 80 short sleeps (up to 20 us) throttle at 32/64 calls; the 65th resets the streak, sleeps 50 us and yields outside the kernel lock. A 5 ms guest sleep does not qualify. Thread argument/stack initialization uses x[0]/x[31].
- [Filesystem](runtime/svc_fs.c): positional reads outside the cache mutex, referenced descriptor lifetime, open/close outside metadata locks, exact RomFS read/error propagation, local copy buffers, and expiring mutation-invalidated negative path caches.
- [Native service/heap hooks](runtime/hle_services.c), [heap adapters](runtime/hle_heaps.c), and [DSP](runtime/audio_effects.c) retain the scoped PrimeRecomp adaptation and [MIT attribution](LICENSES/primerecomp-MIT.txt).

## Validation status

The service/source mismatch is fixed: account existence validates the complete user ID, command 141 fills both auto-select descriptor forms within the caller's buffer, settings guard negative language indices and short device-name buffers, and renderer setup validates bounded mixer counts. CPU-time-limit state is separate from RequestUpdate's frame counter.

The full runner passes 14 native groups plus the compiled NS2 frontend checks:

```sh
python3 scripts/test_runtime_ports.py
```

The installed `bin/runtime_linux_x11` is rebuilt as `v197-ns2-frontend`; existing game executable copies and code modules are not replaced. The previous local runtime binary is retained in Git-local backup storage.

The NS2 checks compile the imported Ampere frontend under C++20 with address/undefined sanitizers, exercise graphics/compute layouts, native capture and mapping faults, forward/backward relocation, and rejection of unknown/indirect instructions. Generated vertex, fragment, and compute shaders pass glslangValidator. This is synthetic coverage, not end-to-end Bananza validation.

Technical architecture, all twelve supplied references, upstream provenance, testing procedure, and remaining gaps are documented in [NS2 technical integration](docs/NS2_TECHNICAL_INTEGRATION.md).

## NS2 integration status

Graphical runtime builds now compile the imported probe and lowering frontend through `runtime/ns2_shader.cpp`. `runtime/gpu_gl.c` accepts explicit `SWR_SHADER_ISA=ampere` selection for native graphics (0x80-byte header) and compute (headerless) shader ingestion. Capture mapping-checks each instruction; the bridge adjusts Eden's scheduling layout and direct branch displacements for the C translator. Native and Maxwell cache identities are separate. Unsupported native instructions or downstream translation failures do not produce executable placeholder shaders.

NS2 `aud:u` commands 1/2 now return the existing C output and renderer manager objects, with real dispatch methods and errors for malformed/unsupported requests. Sysmix, StreamPlay and the other remaining imports are not integrated. In particular, GraphicsSupervisor/GpuWorker transport, QMD3 command execution, native memory ownership, and VI2 presentation remain incomplete. The ISA override does not implement them. No new game-rendering or full NS2 compatibility claim is supported by these tests. The earlier Bananza boot/remote-memory bypasses remain experimental. SDK 22 thread creation now writes the kernel thread ID at TLS+0x110 before publishing the thread; the SDK-owned thread pointer at +0x1f8 is left intact. System-clock context replies use the offset/steady-time/source-ID wire layout also used by clock snapshots.

## Follow-up and diagnosis constraints

1. Implement native graphics-supervisor/GPU-worker ownership and transport before claiming NS2 startup compatibility. Earlier reports describe guards that are no longer in the current kernel: default-core validation and SetThreadPriority bounds must be re-audited rather than assumed present.
2. Investigate FFRS's CPU startup wait; GPU/window changes cannot explain zero GPU submissions by themselves.
3. Collect a new SMW failure log if a crash recurs. The latest examined log closed normally; nearest-symbol labels alone do not prove bad_typeid or bad_array_new_length.
4. Extend SMO/XC2 testing to visible output, audio pacing, gameplay, and long sessions.
5. Review broader borrowed-handle lifetimes and diagnostic-ring publication before claiming synchronization is universally correct.

Do not fabricate ioctl capability words, force syncpoints to completed thresholds during waits, or replace legitimate zero-area draw scissors with full-screen rectangles. Semaphore log `now` is read before writeback. Pointers below the heap base can be valid module mappings; query mapped ranges and permissions.

## Workspace hygiene

[.gitignore](.gitignore) excludes Python caches, local launcher settings, generated game content, and atomic staging scratch names. Ignored files remain on disk. Source changes remain uncommitted. No remote Git sync or sibling-repository mirroring was performed.
