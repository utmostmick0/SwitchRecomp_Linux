# Updated startup and rendering review — 29 October 2026

This update supersedes the earlier review below. Original game logs, saves and settings were preserved. Live runs used isolated data/config copies. No zero-condition-variable success shortcut was added.

## Repairs applied

- **Missing service timer threads:** the runtime ELF carries 0x2a758 bytes (about 174 KiB) of static TLS. Its 64 KiB audio timer stack request could pass attribute setup but fail pthread_create with EINVAL; the failure was ignored. A host-thread trace confirmed the audio timer was absent. POSIX thread creation now retries EINVAL with the platform default stack and frees failed allocations. Renderer timer startup failures are logged. The large-TLS regression test passes.
- **MP4 REV15 audio:** splitter destination records now use revision-specific 0x70 / 0x90 / 0xa8 layouts and correct activity flags. Fixed-point and floating-point biquads are retained and processed; truncated packets, invalid IDs and non-finite coefficients are rejected before configuration changes. Audio time-limit getter/setter metadata and renderer parameter bounds are implemented. Work-buffer sizing reads the effects count from its correct offset.
- **Guest clock rebase race:** samples older than the current anchor no longer underflow unsigned subtraction. Clock sampling under the mutex and monotonic tests cover an adversarial rebase between sample and lock. This is a reproduced correctness repair, not a claim that it caused every observed stall.
- **XC2/PJ037 VSETP:** packed integer component extraction, signed/unsigned comparison and paired predicate writes are translated. Predicate inputs are captured before either output is changed. 1,024 actual host GPU predicate cases passed, including aliasing, plus the three instruction words observed in the logs. Other encodings remain diagnostic.
- **VIC output layout:** luma/chroma dimensions are read independently from the complete output-surface configuration. Linear plane strides use 16-byte alignment; block-linear bounds include GOB padding. Tests cover distinct rows of a 1920-wide NV12 frame, queue ownership, output fences and the synchronous fallback. DK still reports a 2048-byte stride when its allocated luma plane is 2048 wide; this is distinct from the visible 1920 width.
- Unknown shader opcodes now populate the diagnostic text instead of reporting an empty first-untranslated instruction.

Reference layouts were checked against the [Eden VIC implementation](https://git.plexi09.me/Plexi09/eden-miror/src/branch/master/src/video_core/host1x/vic.cpp), its [splitter parameter definitions](https://git.plexi09.me/Plexi09/eden-miror/src/branch/master/src/audio_core/renderer/splitter/splitter_destinations_data.h), and the [VSETP translator](https://git.plexi09.me/Plexi09/eden-miror/src/branch/master/src/shader_recompiler/frontend/maxwell/translate/impl/video_set_predicate.cpp). Code uses local IPC, platform, shader-string and mixer interfaces.

## Live outcomes and remaining limits

| Game | Result after applicable repairs | Remaining issue |
|---|---|---|
| MP4 | REV15 update accepted; audio event stall removed; final screenshot reaches the Metroid Prime 4 title screen; logged loop rate near 60 FPS | Gameplay beyond the title is unverified |
| FFRS | No fatal abort in 45-second test; first GPU submission appears near the end | No presented frame/menu; startup worker remains unfinished |
| Nicola | No fatal abort in 75-second test; reaches four clear/present frames after long asset startup | No drawing shaders/menu; main waits on startup thread |
| PJ037 | No fatal abort; progresses from nine frames to 79 frames late in 35-second test | Other unknown shader instructions and long startup remain |
| TotK | No fatal abort in 35-second test; 1,737 loop frames, 868 RT presentations, 543,440 issued draws | 33 sampler mismatches; a menu stop/visual defect is not excluded by these counters |
| DK | Clean title screenshot; 1920x1088 NV12 jobs execute; logged loop rate near 60 FPS | Does not establish that all movie or gameplay artifacts are fixed |
| SMO | Resume menu renders cleanly in screenshot | User-provided gameplay screenshot shows dark lighting and white HUD; still unresolved |
| XC2 | VSETP warnings gone; about 30 FPS loop rate in 30-second test | Full scene texture correctness not verified; prior readback waits reached 5.9 ms |
| Pokeza | About 60 FPS loop rate, no fatal abort in test window | SURED remains untranslated; cube/2D mismatches and reported rendering issues remain |

The 30/60/unlimited cap settings were retained. Loop FPS and presentation counts describe different work when rendering is capped. Timed test processes were stopped by the harness, so those stops are not counted as crashes. Some earlier runs overlapped read-only diagnostic work; frame-rate figures are not controlled benchmarks.

Evidence: [MP4 title](./games/final-startup-validation-20261029/mp4/frame.png), [timer retests](./games/timer-repair-validation-20261029/), [initial nine-game retests](./games/startup-render-recheck-20261029/), [DK title](./games/vic-stride-validation-20261029/DK/frame.png), [SMO menu](./games/vic-stride-validation-20261029/smo/frame.png). The user's original screenshots were inspected as visual failure evidence.

## Verification

All **16** native runtime check groups now pass, including the previously failing service-port group. Video queue/layout tests passed ASan/UBSan and TSan; the synchronous fallback passed. VSETP arithmetic/predicate behavior was executed on the host OpenGL driver. A full Linux X11 source rebuild passed. Final marker: `v202-service-timers-audio-layout`. Staged to 14 launchers with saves/INIs unchanged. SHA-256: `08a6b25ced7905e0f42b6289d9c9e118d18ac49095f315d0d1d9743d62bbc19f`. No commit or push was made.

---

## Earlier review (historical; superseded above)

# Game log review — 29 October 2026

Reviewed all 14 top-level game logs, including the newest v201 runs. Metrics below are the final logged guest-loop FPS, not proof of visual correctness or physical presentation rate. Several runs use a 30 FPS render cap. Older logs are explicitly identified by their runtime marker. Original logs, saves and settings were preserved.

| Game | Log modified (local) | Runtime | Last logged FPS | Fatal abort |
|---|---|---|---:|---|
| [Balatro](./games/Balatro.log) | 17:44:17 | v199-array-sampler-variants | 12.6 | none logged |
| [Belmonts Curse](<./games/Belmonts Curse.log>) | 17:42:45 | v199-array-sampler-variants | 59.4 | none logged |
| [Bubble Bobble](<./games/Bubble Bobble.log>) | 17:42:26 | v199-array-sampler-variants | 60.0 | none logged |
| [DK](./games/DK.log) | 20:04:27 | v201-ordered-video-worker | 42.9 | none logged |
| [FFRS](./games/FFRS.log) | 20:26:16 | v201-ordered-video-worker | 0.0 | none logged |
| [mp4](./games/mp4.log) | 20:26:20 | v201-ordered-video-worker | — | yes |
| [Nicola](./games/Nicola.log) | 20:25:08 | v201-ordered-video-worker | 0.0 | none logged |
| [PJ037](./games/PJ037.log) | 20:23:31 | v201-ordered-video-worker | 0.8 | none logged |
| [pokeza](./games/pokeza.log) | 20:00:55 | v201-ordered-video-worker | 7.9 | none logged |
| [smo](./games/smo.log) | 17:41:08 | v199-array-sampler-variants | 25.8 | none logged |
| [SMW](./games/SMW.log) | 20:03:39 | v201-ordered-video-worker | 0.0 | none logged |
| [tomadachi life](<./games/tomadachi life.log>) | 20:25:54 | v201-ordered-video-worker | 60.0 | none logged |
| [totk](./games/totk.log) | 20:27:02 | v201-ordered-video-worker | 60.0 | none logged |
| [XC2](./games/XC2.log) | 20:27:28 | v201-ordered-video-worker | 29.8 | none logged |

## Repairs

- Filesystem command 205 now returns the real primary-program RomFS IStorage object for index zero. Unsupported indices and missing arguments fail instead of fabricating a handle. Command signature verified against [Atmosphère’s filesystem interface](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/fssrv/sf/fssrv_sf_i_file_system_proxy.hpp). Regression coverage checks the returned object, GetSize, unsupported index and missing argument.
- Account command 141 now writes the qualified user list through existing descriptor handling. Command 1 verifies the requested 16-byte user ID and rejects short inputs. This restores behavior present in the sibling account service.
- Settings device-name replies now respect short descriptor sizes instead of strcpy writing past the output buffer.
- Audio splitter rejection logs now identify section size and configured/update counts without bypassing validation.

## Isolated live retests

- MP4: original address-zero failure at 0.73 seconds is gone. It reaches seven presented frames and GPU submissions, then aborts on IAudioRenderer RequestUpdate returning 0x899. Rejection narrowed to a splitter packet: 129056 bytes, 128 splitters, 768 destinations, zero info updates and 768 destination updates. The current parser accepts only its older SNDH/SNDI/SNDD layout. Newer packet support is not implemented; no blank-success workaround was added. See [retest log](./games/log-recheck-20261029/mp4/mp4.log).
- SMW: reaches 338 frames, 10889 submitted draws and 169 render-target presentations, then remains at zero FPS. The missing account list is repaired, but the later wait remains unresolved. No crash abort in the retest. See [retest log](./games/log-recheck-20261029/SMW/SMW.log).
- Both retests used isolated save/config copies and linked read-only asset inputs. No original per-game settings were altered.

## Remaining findings

- Nicola: latest original log contains no C7592 errors and compiles no drawing shaders; four clear/present frames then no progress. This log does not establish a vertex-linkage failure.
- FFRS: main thread waits on another thread; workers include condition-variable waits and runnable startup work, no GPU submissions. A 12.53-second run does not prove a permanent I/O deadlock. Kernel wait ownership semantics were preserved.
- XC2 and PJ037: VSETP remains untranslated. XC2 reports no texture mismatches, and its final compute profile has no readback waits. Missing opcode semantics require a verified implementation, not a zero-value replacement.
- TotK, DK, Tomodachi Life, Bubble Bobble and Belmonts Curse show rendering progress without fatal aborts. TotK retains a few texture mismatches. SMO, Balatro and Pokeza have older performance/texture concerns; logged submissions alone cannot validate pixels.

## Verification

Generated vertex and texture shaders, clock retry, IPC sessions, audio effects/routing/update, fonts/button aliases, heap adapters and concurrent filesystem checks passed. Focused account/settings/clock bounds checks passed ASan/UBSan. Buffer delta-upload checks passed. The full runner still stops at test_service_ports.c:66 because the existing audio time-limit getter is an empty stub; the full suite is not green. New repairs do not change that audio API.

Full Linux X11 source rebuild passed and was staged to 14 per-game launchers. Saves, FPS settings and assets were preserved. Binary SHA-256: `a5eeb172786ce0ed5fa9b624d8587d4c66a3839d7e4184697b369cc85b57406c`. Pending video/shader/clock work already in the worktree is included.
