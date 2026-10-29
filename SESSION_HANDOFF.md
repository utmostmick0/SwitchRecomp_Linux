# Runtime handoff

Updated: 2026-10-29. This file is the current status summary; dated sections in older reports are historical.

## Build and launch

- Workspace: `/mnt/Media2/SwitchRecomp_Linux`.
- Runtime: `v199-array-sampler-variants`, [Linux X11 binary](bin/runtime_linux_x11).
- SHA256: `2709826f8e6297b65347f9c8faab97b51996149fbf8ed0738f5a9c2384ba67f2`.
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

The full runner currently stops at service_ports: that test references system_clock_context, which is absent from the current service source. Do not claim all ten groups pass. The shader-generation and RequestUpdate groups pass with sanitizers; generated shaders pass glslangValidator. During this documentation sync, all nine remaining groups passed, including shader validation, clock, IPC, DSP, routing, RequestUpdate, fonts, heaps, and filesystem tests.

```sh
python3 scripts/test_runtime_ports.py
```

To run the currently compatible groups without changing the suite definition:

```sh
python3 - <<'PY'
import scripts.test_runtime_ports as checks
checks.CHECKS = [item for item in checks.CHECKS if item[0] != 'service_ports']
checks.main()
PY
```

## Follow-up and diagnosis constraints

1. Reconcile service_ports with current clock/service source, preserving correct IPC layouts. Earlier reports describe guards that are no longer in the current kernel: default-core validation and SetThreadPriority bounds must be re-audited rather than assumed present.
2. Investigate FFRS's CPU startup wait; GPU/window changes cannot explain zero GPU submissions by themselves.
3. Collect a new SMW failure log if a crash recurs. The latest examined log closed normally; nearest-symbol labels alone do not prove bad_typeid or bad_array_new_length.
4. Extend SMO/XC2 testing to visible output, audio pacing, gameplay, and long sessions.
5. Review broader borrowed-handle lifetimes and diagnostic-ring publication before claiming synchronization is universally correct.

Do not fabricate ioctl capability words, force syncpoints to completed thresholds during waits, or replace legitimate zero-area draw scissors with full-screen rectangles. Semaphore log `now` is read before writeback. Pointers below the heap base can be valid module mappings; query mapped ranges and permissions.

## Workspace hygiene

[.gitignore](.gitignore) excludes Python caches, local launcher settings, generated game content, and atomic staging scratch names. Ignored files remain on disk. Source changes remain uncommitted. No remote Git sync or sibling-repository mirroring was performed.

## Language validation follow-up

The current build also validates all 18 configured language options in IApplicationFunctions Command 21 and set:GetLanguageCode. Invalid indices fall back to packed `en-US`. Command 22 was left unchanged because it is not a language-buffer getter. Sanitized encoding/bounds checks pass; no new gameplay crash fix is claimed. Per-game binaries match the hash above; rollback suffix `.pre-language-validation`.

## Clock registration regression restored

Restored native clock_gettime adapter installation in kernel HLE initialization. The adapter existed but was not registered in the failing v195 launch. Isolated 15-second Vulkan-output tests now reach 511 Belmont frames and 370 Balatro frames at their final periodic summaries, with no recorded fatal error. Startup logs confirm native clock_gettime installation. See [Belmont](games/Belmonts%20Curse.clock-restore-smoke.log) and [Balatro](games/Balatro.clock-restore-smoke.log). Targeted clock, RequestUpdate, and shader groups pass. Command 22 remains a no-output SetTerminateResult acknowledgement; it was not repurposed as a language buffer. Full gameplay remains unverified. Seven game executables match the current hash; rollback suffix `.pre-clock-hook-restore`.

## Large-archive fast path

Large immutable RomFS files (at least 256 MiB) now retain up to eight process-lifetime streams, atomically published after open. Warm reads bypass descriptor-pool searches and refcount locks. A thread-local last-file range avoids repeated metadata binary searches; log-once bookkeeping uses an atomic flag. Bounds checks and exact-read handling remain. Other files retain the referenced LRU fallback. No measured performance gain is claimed.

Sanitized filesystem tests pass with seven concurrent readers beyond 6 GiB, a single retained archive stream, unchanged stream cursor, and a warm read that succeeds without repopulating a cleared descriptor pool. Linux X11 build passes. FFRS still has zero GPU submissions in the isolated 25-second [fast-path test](games/FFRS.archive-fastpath-smoke.log); cache lookup was not a sufficient fix for its startup stall. Runtime staged for seven games; rollback suffix `.pre-archive-fastpath`.

## Condition-wait bypass rolled back

The global zero-key early-success branch in `svc_wait_cv` was removed after the user reported regressions across almost every game. A zero condition key is not proof that waiting is unnecessary; bypassing the atomic mutex-release/wait transition while retaining the guest mutex can starve producers. Restored the pre-bypass path, including its existing bounded infinite-wait behavior. Do not reinstate this global shortcut based only on a zero-valued condition cell.

## All-game startup validation

Completed 14 sequential Vulkan-output tests: cold and warm application shader-cache runs for all seven games, 30-second observation windows. See [validation report](RUNTIME_VALIDATION.md) for logs, screenshots, timing limits and optimization targets. No observed fatal errors; FFRS never submitted a frame, Balatro slowed to about 4 FPS, SMW to 12–14 FPS, and XC2 to about 25 FPS with untranslated VMAD instructions. SMO, Belmont and Bubble Bobble rendered near 60 FPS at their captured menus/demo. Nine native check groups passed; service_ports remains blocked by a missing harness helper. Production source, settings, and saves were unchanged by testing.

## v198 targeted repairs

See [repair report](RUNTIME_REPAIRS.md). Sparse vertex rows no longer trigger a full upload solely through the coarse-block threshold. Fallback font cmap aliases visibly fix SMW L/R prompts, without resolving its sleep loop. Observed integer VMAD forms translate and pass 288 GPU arithmetic cases; unverified modifiers remain unsupported and XC2 still reports ISBERD. Ten compatible native groups and the focused uploader check pass. Seven post-change game runs have no recorded fatal errors; FFRS still never submits and Balatro remains slow. No overall FPS gain is established. The known-bad zero-condition wait bypass was not reapplied. Runtime rebuilt and staged to all seven game launchers, with `.pre-v198-repairs` backups.

## v199 type-5 sampler repair

See [array sampler report](ARRAY_SAMPLER_FIX.md). TIC 5 is 2D array, not 1D array (TIC 4); do not apply the literal Section 3.8 sampler1DArray override to type 5. Compatible plain fetch/sample slots on single-layer type-5 resources now use descriptor-validated sampler2DArray variants with layer zero, explicit cache masks and original slot identity. Thirty-six generated shader cases validate; the reported TotK type-5 warning did not recur in a 45-second test, though 32 unrelated LUT fallback mismatches remain. Rebuilt and staged for 10 games, including newly available totk; backups `.pre-array-sampler`.

## Three-state FPS option

F4 now cycles 60 → 30 → unlimited → 60 in X11 and Windows source, with matching labels and help. An isolated X11 check verified all three persisted values and window titles. Linux runtime rebuilt and staged to 10 game launchers; existing game configs remain at fps_limit=30. Windows behavior was not executable-tested.
