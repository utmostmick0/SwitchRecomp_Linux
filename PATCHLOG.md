# Linux patch milestones

`main` is the pristine Linux v190 AppImage prerelease base. Do not commit development changes to `main`.
`beta` remains the AppImage line; `flatpack` contains Flatpak-specific packaging. Shared runtime and
game-code changes are synchronized between those branches, while packaging-specific changes stay on
their respective branches. Each milestone records the shared changes here and stores a unified patch
from `main` to `beta` in `patches/`.

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

## Recording the next milestone

1. Make shared source changes on either work branch and carry them to the other branch; keep
   Flatpak-specific packaging changes on `flatpack` and AppImage-specific packaging on `beta`.
2. Update this log and keep `main` unchanged.
3. On `beta`, run `scripts/create-milestone-patch.sh NNN` with the next three-digit milestone number.
4. Commit the generated patch and sync the patch and log updates to `flatpack`.
5. Confirm shared source changes match across both branches and `main` has not moved.
