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
- Flatpak remains a draft: it still needs SDL2_image/SDL2_ttf, Python, a compiler, and a
  Flatpak-compatible file chooser before it can package the complete workflow.

The beta patch is `patches/milestone-001-main-to-beta.patch`. It contains the shared memory optimization
and milestone documentation, not the Flatpak-only package changes.

## Milestone 002 — Compile the optimized beta runtime

- Built `bin/runtime_linux_x11` on `beta` with GCC from the updated runtime sources; the build completed
  successfully.
- The rebuilt runtime is synchronized to `flatpack` because it contains the same shared memory code.
- This confirms compilation only. Gameplay and performance measurements are still needed to determine
  whether the memory lookup optimization improves real workloads.

The cumulative beta patch is `patches/milestone-002-main-to-beta.patch`.

## Milestone 003 — Launcher controller detection

- Shared launcher fix on `beta` and `flatpack`: scan for already-connected SDL game controllers during input polling,
  and release disconnected controllers so a later reconnect can be detected.
- This addresses launchers that miss SDL's device-added event when a Steam Controller is connected before startup.
- Rebuilt the Linux launcher with `tool/src/build_tool_linux.sh`.

The cumulative beta patch is `patches/milestone-003-main-to-beta.patch`.

## Milestone 004 — Launcher controller button polling

- Shared launcher change on `beta` and `flatpack`: explicitly enable SDL controller events and poll A, B, Start,
  and Back state as well as handling button events. This supports Steam Input configurations that update state but
  do not reliably emit button-down events.
- Rebuilt the Linux launcher at the repository root as `SwitchRecomp` for direct testing.

The cumulative beta patch is `patches/milestone-004-main-to-beta.patch`.

## Milestone 005 — Linux H.264 video decoding

- Shared runtime change on `beta` and `flatpack`: use Cisco OpenH264 2.4.1's upstream `libopenh264.a`
  when platform-specific split decoder/common archives are unavailable. Windows keeps using its existing
  split archives.
- Added the matching Linux x86_64 static archive, built from Cisco's upstream `v2.4.1` tag to match the
  existing 2.4.1 headers. Build provenance is in `runtime/oh264/lin/README.md`; the BSD license remains in
  `runtime/oh264/LICENSE`.
- Runtime builds use GNU C17 on Linux, preserving the compatibility fix required by GCC 15.
- Rebuilt the Linux X11 runtime with H.264 enabled. Existing game executables need recompilation to use it.
- Flatpak-specific packaging files are unchanged by this shared runtime milestone.

The cumulative beta patch is `patches/milestone-005-main-to-beta.patch`.

## Milestone 006 — Correct audio queue priming

- Shared runtime fix on `beta` and `flatpack`: use a 20 ms audio queue reserve to reduce A/V latency. Playback
  stays at full rate while the queue meets that reserve; adaptive slowdown applies only below it.
- Rebuilt the Linux X11 runtime with the 20 ms reserve; the beta SMO executable now contains this runtime for playback testing.
- The preceding SMO log showed the title music voices beginning at 24.54 seconds and Mario's voice at 25.33
  seconds. These are guest-mix timestamps and do not establish host playback latency or prove a missing channel.
- A previous run reported intermittent audio glitching. The 20 ms build is ready for a playback comparison; no
  underflow counter is currently available, so the log cannot independently confirm an audible glitch.

The cumulative beta patch is `patches/milestone-006-main-to-beta.patch`.

## Milestone 007 — v190 runtime and recompiler fixes

- Synchronized the v190 shared source fixes from beta: GPU color-mask defaults, GPU-side depth copies, queued GPU
  fence waits, compute-dispatch profiling, and GL/shader stall diagnostics.
- Applied the same runtime and recompiler fixes to the Flatpak source tree while retaining its Flatpak-specific
  launcher, data paths, and package manifest.
- The Flatpak runtime binary has not been rebuilt for this milestone.

The cumulative beta patch is `patches/milestone-007-main-to-beta.patch`.

## Milestone 008 — synchronous render-target flushes before GPU copies

- Synced the shared runtime fix from beta: render-target flushes before DMA/2D copies now wait for the current image,
  preventing a delayed PBO readback from overwriting newer guest-memory pixels.
- This addresses the SMO log sequence where the corner-video buffer was repeatedly copied while asynchronous readbacks
  targeted a render-target view at the same guest address.
- The rebuilt beta runtime is ready for visual testing; Flatpak's runtime should be rebuilt from this shared source.

The cumulative beta patch is `patches/milestone-008-main-to-beta.patch`.

## Milestone 009 — preserve GPU-written render targets during texture lookup

- Synced the beta cache fix: texture lookup now keeps GPU-written render targets despite stale guest-memory signatures,
  and clears that ownership marker after synchronized memory/render-target transfers.
- The change targets SMO's repeated log message dropping its 1600x900 background render target at `0x50fbd0000`.
- Rebuild the Flatpak runtime from this shared source after beta's visual retest.

The cumulative beta patch is `patches/milestone-009-main-to-beta.patch`.

## Recording the next milestone

1. Make shared source changes on either work branch and carry them to the other branch; keep
   Flatpak-specific packaging changes on `flatpack` and AppImage-specific packaging on `beta`.
2. Update this log and keep `main` unchanged.
3. On `beta`, run `scripts/create-milestone-patch.sh NNN` with the next three-digit milestone number.
4. Commit the generated patch and sync the patch and log updates to `flatpack`.
5. Confirm shared source changes match across both branches and `main` has not moved.
