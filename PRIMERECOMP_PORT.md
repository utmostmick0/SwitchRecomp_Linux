> Current status: see [SESSION_HANDOFF.md](SESSION_HANDOFF.md). This report retains historical milestones; older runtime markers, hashes, test passes, and failure states do not describe every later source revision.

# PrimeRecomp service integration: testing handoff

Source: `gminustwo/primerecomp`, commit `138318ab06a365b81583ffeae23a472a88483e87`.
Runtime marker: `v190-sonic-cbuf-hle-port`.

The reusable service integration and source-command audit are complete for the selected scope.
The local graphics, input, video, frontend, presentation, audio output, filesystem roots and guest execution backends are preserved.
This does not replace every local stub with an implementation: the source also contains empty-success handlers.
Unsupported source stubs were not promoted into claims of functional support.

## Implemented adaptations

- Account UID validation and qualified-user lists use the local IPC context and bounded B/C descriptors.
- Logger packets retain bounded message, file, function, module and thread fields.
- System-clock context replies carry a consistent offset, steady time point and source ID; the exact SDK current-time hook uses native guest memory.
- Language and application-language replies share all 18 language codes, with a valid fallback for invalid options.
- Renderer factories validate native count limits and bound parameter copies. Work-buffer sizing reads the effect count at its correct parameter offset and returns a page-aligned size.
- Renderer processing-time limit setters/getters have initialized, bounded state.
- Delay, reverb and I3DL2 DSP run inside the existing native mixer, with bounded parameters and explicit owned-state destruction.
- Splitter packets use native renderer-owned state. Complete packet bounds are checked before route updates. Voice and submix routing share the existing mix graph.
- Voice gains ramp across quanta. Stopped voices contribute decaying depop tails. Sparse mix updates preserve unchanged buses.
- Renderer update parsing separates splitter data from mix/sink records instead of treating it as mix data.
- Shared-font SDK hooks adapt BFTTF decoding, TTC face extraction and host font selection to native guest allocations. Font tables and lengths are validated.
- SDK heap adapters retain original guest functions, call them through the native dispatcher, and report allocation failures once per caller/kind.
- Optional unit-heap expansion allocates mapped guest memory and releases it when the guest destroys the heap. Mapping failure falls back to original arguments.
- Parental-control command 1035 and filesystem command 1011 return initialized responses. Native filesystem path reads honor descriptor lengths.

The source [MIT notice](LICENSES/primerecomp-MIT.txt) is retained. No source IPC contexts, session classes, HLE registry macros, settings globals, SDL device backend or FFmpeg dependencies are introduced.

## Source audit and disposition

| Source module | Disposition |
| --- | --- |
| services/acc.c | UID validation/list gaps adapted; native profile and manager objects retained. Native user ID and nickname retained. |
| services/am.c | Native application proxies, subinterfaces, storage, events, launch parameters and applets cover the source commands. Logger field gap adapted. Native docked mode, display size and APM configuration retained instead of source constants. |
| services/time.c | Clock context and direct current-time hook adapted. Native shared-clock memory, snapshots and timezone services retained. |
| services/hid_set.c | Native HID/settings commands and shared memory retained, including supported styles, initialized hold state, vibration interface objects and all language queries. Source SDL rumble/backend replacement excluded. Source's mislabeled handheld getter does not override the existing native command mapping. |
| services/misc.c | Native parental-control and add-on-content objects retained; missing explicit boolean adapted. Native free-communication permission and title-derived DLC base ID retained instead of source's disabled permission and fixed game ID. |
| services/fs.c | Native file, directory, filesystem and RomFS commands retained, including recursive cleanup, enumeration and type-separated saves. Native system archive fallback retained; the source rejects those archives. Missing initialized metadata response adapted. Source's no-op RomFS overlay selector is not imported. |
| services/audio.c | Reusable effects, splitter routes, gain ramps, depop and service response gaps adapted. Native PCM/ADPCM decoding, filtering, AUX rings, pacing, recording, queue ownership, output device and downmix retained. Source diagnostic toggles and direct SDL workers excluded. |
| hle/heaps.c | Original-function wrappers, allocation diagnostics and optional unit-heap growth adapted to native dispatch and mapped guest memory. Existing guest heap sizes remain the default. |
| hle/pl.c | Generic font hooks and container decoding adapted. The existing native pl:u/pl:s IPC font service is retained. |
| hle/bindings.c | Source input/UI backend excluded; local bindings preserved. |
| hle/input.c | Source input/NVN backend excluded; native HID shared-memory path preserved. |
| hle/motion.c | Source controller/sensor backend excluded; native six-axis path preserved. |
| hle/game.c | Title-specific simulation/address hooks excluded; local game execution preserved. |
| hle/movie.c | Source FFmpeg backend excluded; local video decoding preserved. |
| hle/nvn_boot.c | Source NVN registry excluded; local graphics dispatch preserved. |
| hle/swkbd.c | Source menu UI excluded; local keyboard/applet service path preserved. |
| hle/vi.c | Source native-window/display backend excluded; local display, layer and presentation handling preserved. |

## Native ownership and serialization

Imported behavior reads inline parameters with native readers or bounded memcpy, resolves out-of-line buffers with buf_in/buf_out, and translates guest addresses with GP.
New interfaces use svcobj_new/out_obj and native session/domain/handle ownership.
Renderer-owned effect, routing and auxiliary allocations are released by swr_audio_destroy.
The heap adapters use a separate native mutex and do not change g_klock ownership during guest calls.

The heap integration defaults to scale 1. Optional `SWR_UNIT_HEAP_SCALE=2` through `8` enables expanded unit heaps without changing the host allocator ABI.
`SWR_NO_HLE=1` still disables all SDK hooks. `SWR_FONT` selects a font for the direct SDK font hooks; existing IPC font selection remains unchanged.

## Automated validation

Run from the repository root:

```sh
python3 scripts/test_runtime_ports.py
python3 switchrecomp.py --build-runtime --target linux --backend x11 --cc gcc
```

Eight native check groups cover:

1. Service marshalling, UID equality, B/C lists, short buffers, profile/manager factories, clock IDs, language selection and renderer limits.
2. Real IPC transport and handle-table ownership: returned sessions, cloning, domain conversion, object calls and closes.
3. DSP impulse timing, cross-block history, channel remaps, malformed parameters, type changes and destruction.
4. Actual mixer voice/submix splitter routing, sparse updates, volume ramps, depop, packet truncation and renderer cleanup.
5. Actual renderer RequestUpdate section layout, sparse mix packets, truncated input and invalid total lengths.
6. Synthetic sfnt/TTC/BFTTF decoding, truncated containers and overflowing table ranges.
7. Heap original-call continuation, return preservation, mapped-buffer release, mapping-failure fallback and bounded diagnostics.
8. Native filesystem path descriptor lengths and mount traversal rejection.

Checks use undefined-behavior sanitizers; the standalone state/transport checks additionally use address and floating-point conversion sanitizers.
No proprietary font fixtures or game files are required for these checks.

## Game testing

Use the rebuilt runtime executable with the existing game code library and data directory.
Confirm the first log line contains `v190-sonic-cbuf-hle-port` before comparing runs.
Test loading, sound effects/music, pause/resume, scene changes, save/load, fonts, controller input and normal exit.
The previous Belmont worker-thread C++ exception is a separate observed failure; these unit/integration checks alone do not establish that it is fixed.

## Prepared testing build

The final X11 runtime rebuilt successfully. All eight automated native check groups passed.
The following game executables were updated atomically to that runtime: Balatro, Belmonts Curse, Bubble Bobble, FFRS, SMW and smo.
Each previous executable is retained beside it with the suffix `.pre-hle-port`; restoring that copy rolls back the runtime without changing game code or saves.

Runtime SHA-256: `d04e67e6e021f78f707b3208c2fd7185bd98b3abef5b262528d3e12d1f77a7bd`.

An isolated Belmont startup used separate configuration/save/cache paths and the existing extracted game data and code library.
It confirmed the new runtime marker and rendering activity, but reproduced the existing worker-thread C++ termination / svcBreak at 4.46 seconds.
The run was stopped after 18 seconds. This is a known game-test failure, not a successful gameplay test.
The captured log is `Belmonts Curse.hle-port-smoke.log` (historical capture; no longer present in the workspace).
The regular game log and saves were not replaced by that smoke test.


## All-game log follow-up (2026-10-29)

Runtime v191-audren-buffer-capacity is rebuilt and staged for all six launchers.
SHA256: `bd2c06bc67b5b58478683017e50979efb1bbbc1d8ae6bf20881feb496b997fec`.
Rollback copies for this update use `.pre-log-fixes`.

The new renderer-open check incorrectly applied a per-bus 24-channel limit to
its total buffer capacity. The mixer supports 64 buses with 24 channels each;
total capacity now accepts up to 1536 while individual bus bounds remain intact.
Regression coverage checks acceptance above 24 and rejection above 1536.
All eight native check groups and the full Linux X11 build pass.

| Game | Latest user log and follow-up |
| --- | --- |
| Balatro | No fatal error in the latest 10-second run; drawing and audio active. |
| Bubble Bobble | No fatal error in the latest 4.46-second run; texture and GPU work active. |
| SMW | Renderer rejection fixed (108 buffers). Isolated 18-second check advances beyond the previous null call to 59 presented frames, then stalls. |
| smo | Renderer rejection fixed (42 buffers). Isolated 18-second check reaches REV3 audio updates but still stalls before the first presented frame. |
| FFRS | Isolated 30-second check still stalls before any GPU submission. |
| Belmonts Curse | Bounded AArch64 vararg diagnostics identify the actual uncaught exception: `std::__1::system_error`, `clock_gettime(CLOCK_REALTIME) failed: Resource temporarily unavailable`. Remains unresolved. Previous nearest-symbol array/RTTI labels were not the cause. |

Isolated follow-up logs use `games/<game>.log-fixes-smoke.log`; regular launch logs
and saves were preserved. These checks do not establish successful gameplay.
The exception diagnostic only reads mapped, readable guest ranges and leaves
normal exception handling unchanged. Next Belmont investigation should trace
the SDK realtime clock callback and initialization state instead of adding
thread-name or allocation clamps.


## Native clock follow-up (2026-10-29)

Runtime `v192-native-clock-retries` supersedes the earlier Belmont failure status above.
The exported libc `clock_gettime` uses a separate SDK callback from the existing StandardUserSystemClock hook. A native adapter now supplies initialized realtime and monotonic timespecs through that entry. Unsupported clock IDs, invalid output buffers, and permanent host failures retain the original guest handler.

Linux realtime reads retry EAGAIN/EINTR at most eight times, then try the wall-clock seconds API. Success is returned only after a valid timestamp is written; no fabricated handle or timestamp token is used. Windows uses its FILETIME wall clock.

All nine native check groups pass, including injected EAGAIN, EINTR, retry exhaustion, permanent failures, output bounds, and original-handler fallback. The Linux X11 build passes. Windows compilation was not run.

An isolated 22-second Belmont run logged no fatal exception and reached 1,097 frames at 21.67 seconds. This clears the previously reproducible ~4-second startup exception in this test, but is not a full gameplay pass. See `Belmonts Curse.clock-smoke.log` (historical capture; no longer present in the workspace). Regular logs and saves were preserved.

Rebuilt runtime staged for all six games; rollback copies use `.pre-clock-retries`. SHA256: `ab35f7d65b02a3333451b35a6efefe9a27c481bfa3eb2e76abcadbd4817836c8`.


## Audio configuration and positional reads (2026-10-29)

Runtime `v193-positional-file-reads` is rebuilt and staged for all six games.
The RomFS archive path already used position-relative reads outside its metadata lock; that ownership protection is retained. Remaining IFile reads now use bounded 1 MiB positional chunks with offset checks, short-read handling, and Linux EINTR retries. Buffered writes are flushed so subsequent pread calls see them.

Audio checks confirm that SMO's 96 voices and 14 effects are retained in the renderer configuration. The SDK configuration getters return sample rate, sample count, mix-buffer count, and state; no unsupported voice/effect register fields were invented.

All nine native groups pass, including seven concurrent readers of separate blocks beyond 6 GiB, unchanged file cursor, and write/read coherence. Full Linux X11 build passes. No new gameplay or stall-resolution claim is made. Rollbacks use `.pre-positional-reads`.

SHA256: `2714d75d8b3f72234887005950bd2f3ec9801faf577f7773cd5780b1fbd8ac3d`.
