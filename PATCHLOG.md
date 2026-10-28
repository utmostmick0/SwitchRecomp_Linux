# Linux patch milestones

`main` is the pristine Linux v190 AppImage prerelease base. Do not commit development changes to `main`.
`beta` remains the AppImage line; `flatpack` contains Flatpak-specific packaging. Shared runtime and
game-code changes are synchronized between those branches, while packaging-specific changes stay on
their respective branches. Each milestone records the shared changes here and stores a unified patch
from `main` to `beta` in `patches/`.

Generate the cumulative patch on `beta` with `scripts/create-milestone-patch.sh NNN`. It contains the
committed `main`-to-`beta` source, binary, and documentation changes (including this log), encoded as a
binary-capable Git diff. Earlier patch files are excluded to avoid nesting patches. Apply it from a clean
`main` checkout with `git apply --binary patches/milestone-NNN-main-to-beta.patch`.

## Milestone 001 — Flatpak foundation and memory lookup

- Shared across `flatpack` and `beta`: optimized guest memory block lookup using the existing sorted
  block list.
- `flatpack` only: changed Linux defaults to XDG data/config paths, added writable directory creation,
  and added an initial Flatpak manifest, launcher, desktop entry, AppStream metadata, and Steam Deck notes.
- `beta` keeps the original AppImage packaging behavior from `main`.
- Flatpak remains a draft: it still needs the SDL3 stack, Python, a compiler, and a
  Flatpak-compatible file chooser before it can package the complete workflow.

The beta patch is `patches/milestone-001-main-to-beta.patch`. It contains the shared memory optimization
and milestone documentation, not the Flatpak-only package changes.

## Milestone 002 — SDL3 migration

- Replaced SDL2 in the Linux launcher with SDL3, SDL3_ttf, and SDL3_image APIs for rendering, text, image
  handling, menu gamepad input, and music playback.
- Replaced the runtime's optional SDL2 gamepad and queued-audio backends with SDL3 gamepad and audio-stream
  APIs. Both still load SDL3 at runtime so games retain keyboard/playback fallback when SDL3 is unavailable.
- The launcher now enumerates already-connected gamepads on startup and polls confirm/cancel/menu buttons
  in addition to event handling.
- Updated Linux build and dependency instructions. The Windows tool continues to use SDL2 libraries.
- The Linux launcher build uses GNU C17 so GCC 15's C23 default does not make the older-glibc compatibility
  wrappers recurse into themselves at SDL3 startup. The build script writes the launcher beside `bin/` and
  `tool/`, matching the paths expected by the app.
- The Linux runtime build also uses GNU C17. Without it, GCC 15 emits C23 `strtol` calls that resolve to the
  compatibility wrapper and recurse while reading a game's INI file; SMO then consumed a full CPU core
  before creating its window. Rebuilds through `switchrecomp.py --build-runtime` now include the same flag.
- `SDL_GetBasePath()` returns an SDL-owned cached path in SDL3, so the launcher keeps it as a `const char *`
  and does not free it. This avoids a shutdown double free.
- SDL3 gamepad button events and polling now trigger A, B, and menu actions only on press edges. Previously,
  holding A while a game tile was selected could launch a new game process every frame.
- Verified the Linux launcher and X11 runtime compile with GCC against SDL3 3.4.18, SDL3_ttf 3.2.2, and
  SDL3_image 3.2.4. The Steam Controller is recognized by SDL3; gamepad behavior in a running game still
  needs a successful SMO launch to verify.

The cumulative beta patch is `patches/milestone-002-main-to-beta.patch`.

## Milestone 003 — Linux H.264 video decoding

- Added a Linux x86_64 OpenH264 2.4.1 static archive, built from Cisco's upstream `v2.4.1` tag to match
  the runtime's existing 2.4.1 API headers. The upstream BSD license is retained in `runtime/oh264/LICENSE`.
- Updated runtime builds to use the upstream `libopenh264.a` archive when the platform-specific split
  decoder/common archives are unavailable. Windows continues using its existing split archives.
- Rebuilt `bin/runtime_linux_x11` with H.264 enabled. Existing game executables must be recompiled with this
  runtime to decode H.264 movies. Recompiled the local SMO game executable using its existing execution
  hints so it is ready for movie playback testing.
- Build provenance and the reproducible OpenH264 command are recorded in `runtime/oh264/lin/README.md`.

The cumulative beta patch is `patches/milestone-003-main-to-beta.patch`.

## Milestone 004 — Correct audio queue priming

- Shared runtime fix on `beta` and `flatpack`: use a 20 ms audio queue reserve to reduce A/V latency. Playback
  stays at full rate while the queue meets that reserve; adaptive slowdown applies only below it.
- Rebuilt the Linux X11 runtime with the 20 ms reserve and installed it into the local SMO executable for testing.
- The preceding SMO log showed the title music voices beginning at 24.54 seconds and Mario's voice at 25.33
  seconds. These are guest-mix timestamps and do not establish host playback latency or prove a missing channel.
- A previous run reported intermittent audio glitching. The 20 ms build is ready for a playback comparison; no
  underflow counter is currently available, so the log cannot independently confirm an audible glitch.

The cumulative beta patch is `patches/milestone-004-main-to-beta.patch`.

## Milestone 005 — v190 runtime and recompiler fixes

- Ported the v190 source changes from `SwitchRecomp_v190_linux.tar.gz` into the shared runtime and tool code.
- Initialized GPU channel color masks so GBA Classics' loading UI is not discarded, kept supported depth-buffer copies
  on the GPU for Sonic Frontiers, and moved submit-fence waits onto the GPU queue to avoid blocking the game thread.
- Added per-compute-dispatch GPU timing entries and more detailed GPU/GL/compiler-thread stall diagnostics. GL fence
  polling now takes the driver lock to avoid concurrent driver calls during shader compilation.
- Merged the recompiler and shader changes while retaining beta's SDL3 launcher and build configuration. The archive's
  SDL2 standalone installer packaging is not part of the beta AppImage line.
- Rebuilt the SDL3 launcher and Linux X11 runtime from the updated source. The launcher build completed with existing
  `-Wformat-truncation` path warnings; both builds completed successfully.
- Existing recompiled games need their runtime updated or the games recompiled before these runtime fixes take effect.

The cumulative beta patch is `patches/milestone-005-main-to-beta.patch`.

## Milestone 006 — synchronous render-target flushes before GPU copies

- SMO's live log showed its 480x544 corner-video output sharing guest memory with a render-target view, followed by
  repeated DMA copies and asynchronous render-target readbacks at that address.
- Made render-target flushes synchronous before DMA/2D engine copies. A delayed PBO readback could otherwise write an
  older render-target image into guest memory after the copy had updated video or background pixels.
- Rebuilt the Linux X11 runtime and copied it over `games/smo` so SMO uses the rebuilt runtime. `games/smo_code.so`
  remains unchanged; the game code did not need recompilation for this runtime change.
- This is a targeted correctness fix based on the logged copy/readback ordering; it still needs visual confirmation in
  SMO because the runtime log does not capture rendered pixels.

The cumulative beta patch is `patches/milestone-006-main-to-beta.patch`.

## Milestone 007 — preserve GPU-written render targets during texture lookup

- SMO's retest confirmed that synchronous DMA/2D flushes removed the asynchronous readback path, but the 1600x900
  background render target at `0x50fbd0000` was still repeatedly discarded when its guest-memory signature differed.
- Texture lookup now trusts a render target marked GPU-written instead of comparing it to stale guest memory. A
  synchronous flush or memory-to-render-target reload clears that ownership marker after updating the signature.
- This fixes the cache decision implicated by the new log. Rebuilt the Linux X11 runtime and copied it into
  `games/smo` after the game exited; `games/smo_code.so` remains unchanged.
- Regenerated the cumulative milestone patch against the v190 release archive now on `main`.

The cumulative beta patch is `patches/milestone-007-main-to-beta.patch`.

## Milestone 008 — OpenGL geometry shader support

- Ported the geometry shader translation and draw-pipeline support from `Switchrecomp0` into the shared Linux runtime.
- Geometry shaders now declare primitive input/output layouts, read indexed per-vertex attributes, apply the runtime
  viewport/depth transform to emitted vertices, and bind their separable GL program and stage resources.
- Geometry shader draws require separable program pipeline support. Tessellation stages remain unsupported; when
  separable pipelines are unavailable, the runtime logs and draws without the geometry stage.
- Synchronized the shared source changes to `flatpack`; packaging-specific files were not changed. Rebuilt the Linux
  X11 runtime on both work branches to check compilation. Game-level rendering still needs validation with a title that
  uses geometry shaders.

The cumulative beta patch is `patches/milestone-008-main-to-beta.patch`.

## Milestone 009 — shader opcode lookup table

- Ported the shader decoder lookup optimization from Metroid Dread Recomp. It precomputes the first matching opcode
  for each 16-bit instruction prefix, replacing a linear pattern scan for each decoded instruction.
- Retained beta's shader patterns and special EXIT handling. No Dread-specific Vulkan, Android, or audio code was
  copied because those changes do not match this OpenGL runtime.
- Rebuilt beta's Linux X11 runtime; the Flatpak runtime also needs rebuilding before that package uses this change.

The cumulative beta patch is `patches/milestone-009-main-to-beta.patch`.

## Milestone 010 — 30 FPS presentation cap

- `fps_limit=30` now skips every other presented image while keeping the game's normal 60 Hz update pacing. The
  existing `fps_target` render-skip setting remains available separately.
- The cap handles both GPU-backed and CPU-converted presentation paths. Rebuilt the beta Linux X11 runtime; the
  Flatpak runtime also needs rebuilding before use.

The cumulative beta patch is `patches/milestone-010-main-to-beta.patch`.

## Milestone 011 — rsync ROMFS copying on Linux

- Linux ROMFS copies now use `rsync` to transfer only files that are new or changed, preserving
  timestamps and the existing two-second mtime tolerance. Extra destination files are left intact.
- Windows and Linux installations without `rsync` continue to use the existing Python copy path;
  the tool reports when Linux falls back because `rsync` is unavailable.
- The same change is synchronized to the Flatpak branch. No game/runtime rebuild is needed; the
  Python recompiler takes effect the next time a game is copied.

The cumulative beta patch is `patches/milestone-011-main-to-beta.patch`.

## Milestone 012 — XCDE shader limits and texture formats

- XCDE's latest log showed two vertex shaders failing with NVIDIA's `no buffers available for bindable uniform cb15`.
  The translator now checks per-stage OpenGL uniform-block limits and moves only excess guest constant buffers to
  read-only storage blocks, with the runtime binding those buffers through the existing upload path.
- Added OpenGL mappings for the logged G24R8 and RGBX16SN texture formats. RGBX16SN forces its unused X component
  to opaque alpha when sampled.
- The trace manifests in `games/captures/traces/1` through `9` report no untranslated or failed shaders in their
  recorded draws. The two titles that submit one frame and then stop, and SEVEN_Produ's null-address instruction
  fetch, remain separate game startup failures without a safe runtime fix identified from these traces.
- Runtime sources changed on both beta and Flatpak. Rebuild each runtime before testing; games do not need
  recompilation.

The cumulative beta patch is `patches/milestone-012-main-to-beta.patch`.

## Milestone 013 — VP9 WebM playback

- Added the libvpx VP9 decoder path adapted from [Metroid Dread Recomp](https://github.com/DaniSald/Metroid-Dread-Recomp/tree/main/runtime). It indexes WebM files read from RomFS, matches NVDEC jobs to frames, decodes VP9, and associates decoded frames with VIC output surfaces.
- Vendored libvpx 1.14.1's public headers and BSD-3-Clause license, plus a static x86-64 Linux decoder archive. The runtime stays self-contained and does not need a system libvpx package.
- Added optional VP9 linkage to the runtime build and updated the Linux notes. The change is synchronized to `flatpack`; both X11 runtimes compile with the decoder linked.
- XCDE's earlier VP9 movie log motivated this change. The latest log has not yet been rechecked with this decoder, so movie playback and frame matching still need an in-game test. Update XCDE from `bin/runtime_linux_x11` using the launcher's **Runtime** option before testing.

The cumulative beta patch is `patches/milestone-013-main-to-beta.patch`.

## Milestone 014 — Game-labeled frame traces

- F5 frame trace sets are now written to `captures/traces/<sanitized-game-title>_<number>/`, so captures from different games are easy to distinguish and each game's sequence starts at 1.
- The original game title is also recorded in each `trace.txt`. Existing numeric trace folders are left intact.
- The runtime source change is synchronized to `flatpack`, and both Linux X11 runtimes have been rebuilt. Update each installed game's runtime from the launcher before recording tagged traces.
- The cumulative beta patch is `patches/milestone-014-main-to-beta.patch`; it is generated from the committed `main..beta` history and excludes previous patch files.

## Milestone 015 — Linux Vulkan output prototype

- Added `renderer=vulkan` on the Linux X11 backend while keeping the existing OpenGL game renderer. The backend reads
  each completed GL frame into host memory, uploads it through a Vulkan staging buffer, and presents it with an Xlib
  swapchain. Startup and fallback reasons are logged; Vulkan is loaded only when requested.
- This first path is for compatibility and can cost significant CPU time. It does not use the Dread Windows-only
  zero-copy interop path and has not yet been tested in-game. Use `renderer=opengl` to restore the default path.
- The source and usage notes are synchronized to `flatpack`; both X11 runtime binaries were rebuilt. Installed games
  still need their runtime updated before testing. The cumulative patch is `patches/milestone-015-main-to-beta.patch`.

## Milestone 016 — Automatic trace archives

- Completed Linux X11 F5 traces now produce a sibling `.zip` archive automatically. The original trace folder stays in place, and the archive is written without an external `zip` utility so the Flatpak build can do the same.
- The ZIP uses the standard store method (no compression): it groups the trace files into one portable file, but does not reduce their total size.
- Runtime and build changes are synchronized between `beta` and `flatpack`. Rebuild/update the Linux X11 runtime before the feature is available in installed games.
- The cumulative beta patch is `patches/milestone-016-main-to-beta.patch`.

## Milestone 017 — Exact vertex buffer mirror checks

- Completed the recent graphics performance and presentation changes: 512 MiB buffer-cache pruning after two inactive
  frames over budget, 64 KiB delta uploads, transient RT format warning suppression, safe crop defaults, and a more
  exact render-target match for presentation. Stencil-only format 0x17 uses a stencil attachment.
- Added a host yield after 64 consecutive zero-timeout synchronization polls, outside the kernel wait lock.
- Replaced hash-only delta validation with a byte-exact CPU mirror for cached guest buffer ranges. When a range is
  checked, identical bytes bypass OpenGL uploads; changed 64 KiB runs use `glBufferSubData`, with a full sub-data
  upload when most of the range changed. This prevents hash collisions from silently treating changed geometry as
  unchanged.
- Exact mirrors are capped at 512 MiB of host RAM. If that budget is reached or allocation fails, the existing hash
  fallback remains available. Compute-generated buffer copies invalidate their CPU mirrors before the later guest
  memory reread.
- Disabled the sampled/stability shortcuts for ranges with an exact mirror so every mirror-backed validation uses
  the complete byte comparison. Added mirror usage to periodic GPU memory telemetry.
- Rebuilt `bin/runtime_linux_x11` successfully with the Linux X11 backend. In-game performance and memory usage still
  need validation.

The cumulative beta patch is `patches/milestone-017-main-to-beta.patch`.

## Milestone 018 — Combined buffer and mirror cache pruning

- The two-frame stale buffer sweep now activates when GPU buffer allocations plus exact CPU mirror allocations
  exceed 512 MiB. Previously the trigger counted only GPU buffers, even though mirror memory could add another
  512 MiB.
- The pruning diagnostic reports the combined allocation total. Entries touched within the last two active frames
  remain protected; stale entries release both the GL buffer and its exact mirror.
- Rebuilt the beta Linux X11 runtime. The latest Balatro trace remains GPU-bound in heavy scenes (11–16 FPS at
  86–91% GPU busy), with 180–232 MiB of geometry uploaded per frame. This cache change addresses memory pressure;
  it does not reduce geometry that changes every frame.

The cumulative beta patch is `patches/milestone-018-main-to-beta.patch`.

## Milestone 019 — Cached contiguous GVA validation

- Added a per-thread, exact-range cache for successful `gmem_ptr()` contiguous GMMU validations. Repeated guest
  virtual address and size pairs can bypass the page-by-page translation walk for up to 30 rendered frames.
- GMMU map and unmap operations advance a generation counter; cached ranges are rejected while a mapping update is
  in progress and invalidated immediately when the generation changes.
- Kept byte-exact buffer mirror comparisons enabled. Linux does not track every direct guest CPU store in this
  runtime, so skipping content checks based only on a repeated address could render stale geometry.
- Rebuilt beta's Linux X11 runtime successfully. Frame-rate impact needs a fresh Balatro run.

The cumulative beta patch is `patches/milestone-019-main-to-beta.patch`.

## Milestone 020 — Orphaned uniform upload arena

- Replaced the persistent-mapped constant-buffer ring and its synchronous segment-fence waits with a mutable
  32 MiB uniform upload arena. Uploads use `glBufferSubData`; when the arena reaches its end, the runtime
  orphans its storage with `glBufferData(..., NULL, ...)`, letting queued draws retain the old backing store
  while subsequent uploads use fresh storage.
- Cache entries now include the arena store generation so a wrapped/orphaned allocation can never reuse a
  constant-buffer offset from the prior backing store. The runtime profile reports arena orphan counts in
  place of the removed ring-wait count.
- Rebuilt `bin/runtime_linux_x11` successfully with the Linux X11 backend. Game performance and rendering
  correctness need validation in a fresh game run; replacing persistent mapping with `glBufferSubData` may
  change upload throughput on some drivers.

The cumulative beta patch is `patches/milestone-020-main-to-beta.patch`.

## Milestone 021 — Rebind uniform ranges after orphaning

- After orphaning the uniform arena, the runtime now rebinds the arena to both generic UBO and SSBO targets,
  then reasserts previously cached indexed ranges. It clears the software range cache so subsequent constant
  uploads explicitly bind their newly populated slices.
- Rebuilt `bin/runtime_linux_x11` successfully. This is a driver-state workaround; a fresh game run is needed
  to confirm whether it resolves the reported black screen.

The cumulative beta patch is `patches/milestone-021-main-to-beta.patch`.

## Milestone 022 — DMA block-linear bounds validation

- Added checked block-linear row-pitch and surface-footprint calculations for DMA uploads. Dimensions above
  16384, block-height/depth exponents above 5, overflow-sized rows, and layouts above 256 MiB are rejected
  before span lookup or copy.
- DMA rectangles are clipped to the declared block-linear source and destination width/height. Linear row
  copies with a pitch shorter than the copied row are rejected. GOB pitches are normalized to 64-byte
  boundaries; this preserves the block-linear address calculation while making the footprint arithmetic
  explicit and overflow checked.
- The available Balatro trace shows valid `lin->bl` transfers into a 2048x1024 surface with block-height
  exponent 4; these rows stay within its bounds. The FFRS log has no frame submissions or DMA copies, so
  it does not identify this path as the confirmed cause of its black screen.
- Rebuilt `bin/runtime_linux_x11` successfully. Fresh Balatro and FFRS runs are still needed to verify the
  guarded path and investigate any remaining presentation failure.

The cumulative beta patch is `patches/milestone-022-main-to-beta.patch`.

## Milestone 023 — Gate framebuffer lookups on active descriptors

- Added a shared framebuffer descriptor check requiring nonzero width/height/address and a mapped guest base
  before render-target format resolution, texture creation, framebuffer binding, or format-key derivation.
- `fs_get_keyed` now skips incomplete color descriptors and only performs sampler/TIC state lookup when a
  valid color or enabled depth target exists. This keeps intermediate register state out of format and active
  texture resolution. The existing pending-draw flush runs after MME method emission, so the lookup sees the
  final register state when the descriptor is complete.
- Rebuilt `bin/runtime_linux_x11` successfully. A fresh FFRS run is still needed to determine whether this
  addresses its black screen.

The cumulative beta patch is `patches/milestone-023-main-to-beta.patch`.

## Recording the next milestone

1. Make shared source changes on either work branch and carry them to the other branch; keep
   Flatpak-specific packaging changes on `flatpack` and AppImage-specific packaging on `beta`.
2. Update this log and keep `main` unchanged.
3. On `beta`, run `scripts/create-milestone-patch.sh NNN` with the next three-digit milestone number.
4. Commit the generated patch and sync the patch and log updates to `flatpack`.
5. Confirm shared source changes match across both branches and `main` has not moved.
