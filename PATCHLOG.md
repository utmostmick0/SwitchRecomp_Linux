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
- Verified the Linux launcher and X11 runtime compile with GCC against SDL3 3.4.18, SDL3_ttf 3.2.2, and
  SDL3_image 3.2.4. Runtime controller and audio behavior still needs testing with a Steam Controller.

The cumulative beta patch is `patches/milestone-002-main-to-beta.patch`.

## Recording the next milestone

1. Make shared source changes on either work branch and carry them to the other branch; keep
   Flatpak-specific packaging changes on `flatpack` and AppImage-specific packaging on `beta`.
2. Update this log and keep `main` unchanged.
3. On `beta`, run `scripts/create-milestone-patch.sh NNN` with the next three-digit milestone number.
4. Commit the generated patch and sync the patch and log updates to `flatpack`.
5. Confirm shared source changes match across both branches and `main` has not moved.
