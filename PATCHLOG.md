# Linux patch milestones

`main` is the pristine Linux v190 prerelease base. Do not commit development changes to `main`.
Source changes are kept in sync on `flatpack` and `beta`. Each milestone records the changes here and
stores a unified patch from `main` to `beta` in `patches/`.

## Milestone 001 — Flatpak foundation and memory lookup

- Changed Linux defaults so games and settings live under the XDG data and config directories. Added
  recursive directory creation for these writable paths.
- Added an initial Flatpak manifest, launcher, desktop entry, AppStream metadata, and Steam Deck notes.
- Optimized guest memory block lookup using the existing sorted block list.
- Kept this milestone marked as a draft: the manifest still needs SDL2_image/SDL2_ttf, Python, a
  compiler, and a Flatpak-compatible file chooser before it can package the complete workflow.

The milestone patch is `patches/milestone-001-main-to-beta.patch`.

## Recording the next milestone

1. Make and document source changes on the active development branch.
2. Sync those commits to the other development branch. Keep `main` unchanged.
3. On `beta`, run `scripts/create-milestone-patch.sh NNN` with the next three-digit milestone number.
4. Commit the generated patch and sync that commit to `flatpack` so both branches have the same source,
   documentation, and patch artifact.
5. Confirm `git diff flatpack beta` is empty and `main` has not moved.
