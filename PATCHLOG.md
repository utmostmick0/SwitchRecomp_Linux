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

## Recording the next milestone

1. Make shared source changes on either work branch and carry them to the other branch; keep
   Flatpak-specific packaging changes on `flatpack` and AppImage-specific packaging on `beta`.
2. Update this log and keep `main` unchanged.
3. On `beta`, run `scripts/create-milestone-patch.sh NNN` with the next three-digit milestone number.
4. Commit the generated patch and sync the patch and log updates to `flatpack`.
5. Confirm shared source changes match across both branches and `main` has not moved.
