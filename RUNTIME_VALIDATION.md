# Runtime validation — 2026-10-29

Runtime SHA-256: `bf66f7bb3c70538d044690563c90d37bf69bd4e93b1cdc33759e08f271f99119`. The global zero-condition-key shortcut is absent.

## Method

Seven games, sequential Vulkan/Xlib-output startup tests, two 30-second observation windows per game. Cold means a new application shader-cache directory; warm reuses that directory. Host filesystem and GPU driver caches were not flushed. Original settings, hints, and existing saves were copied into isolated test folders; game assets were linked. Originals were preserved. Warm tests retain any changes to the isolated saves. No automated gameplay inputs were sent. Periodic X11 screenshots add some overhead.

Runs were terminated by the harness after the observation window; signal exits in `runs.json` are test shutdowns, not observed crashes. Analysis below excludes log samples after 30 seconds. A single pair is exploratory, not a statistically controlled benchmark. Vulkan output uses the existing OpenGL renderer plus CPU upload to the Vulkan window.

## Measurements

“Frame summary” is the first periodic summary reporting frames greater than zero, an upper bound on first presentation, **not** a measured loading-completion time. FPS is the last sampled interval before 30 seconds. Buffer registration is an initialization milestone, not visible-content readiness.

| Game | Cache | VI buffer registered (s) | First nonzero frame summary (s) | Frames / draws at last sample | Last FPS | GPU thread busy | Texture mismatch / failed shader |
|---|---|---:|---:|---|---:|---:|---|
| Balatro | cold | 1.53 | 7.3 | 388 / 6238 | 4.1 | 99% | 0 / 0 |
| Balatro | warm | 1.24 | 7.09 | 397 / 6447 | 4.2 | 99% | 0 / 0 |
| Belmonts Curse | cold | 3.9 | 13.03 | 1054 / 35096 | 58.6 | 16% | 0 / 0 |
| Belmonts Curse | warm | 1.66 | 7.28 | 1234 / 43057 | 58.4 | 18% | 0 / 0 |
| Bubble Bobble | cold | 1.53 | 7.27 | 1337 / 8569 | 60.0 | 13% | 0 / 0 |
| Bubble Bobble | warm | 1.09 | 7.06 | 1362 / 8906 | 60.0 | 14% | 0 / 0 |
| FFRS | cold | — | — | 0 / 0 | 0.0 | 0% | 0 / 0 |
| FFRS | warm | — | — | 0 / 0 | 0.0 | 0% | 0 / 0 |
| SMW | cold | 3.78 | 13.04 | 511 / 15110 | 12.5 | 7% | 0 / 0 |
| SMW | warm | 1.09 | 7.07 | 520 / 17831 | 14.2 | 7% | 0 / 0 |
| XC2 | cold | 1.96 | 7.44 | 562 / 263866 | 24.5 | 63% | 0 / 0 |
| XC2 | warm | 1.31 | 7.16 | 555 / 266783 | 25.4 | 63% | 0 / 0 |
| smo | cold | 2.58 | 12.73 | 1197 / 23860 | 60.0 | 17% | 0 / 0 |
| smo | warm | 1.25 | 7.2 | 1320 / 24900 | 60.0 | 17% | 0 / 0 |

## Findings and optimization targets

- **Balatro:** frames render, but performance collapses to about 4 FPS in both runs. At the late observation sample the runtime checks/uploads roughly 330 MiB of vertex/index data per frame, with GPU-thread busy around 99%. The card artwork is visible; screenshot alone does not establish animation correctness. Investigate growing draw counts, buffer-range sizing and repeated uploads first. The current attribute path uploads the declared guest buffer range, capped at 64 MiB per binding; do not shorten indexed ranges without proving which vertices are accessed. Warm cache does not resolve this slowdown.
- **Belmont’s Curse:** artwork menu renders near 59 FPS. No fatal exception observed. Cold/warm startup differs, but shader cache, filesystem cache, and isolated-save changes are confounded. Null texture bindings and one unsupported-format-0 warning require shader-use/visual correlation; zero type mismatches does not prove all textures correct.
- **Bubble Bobble:** attract/demo content renders near 60 FPS. No failed shaders or texture-type mismatches reported.
- **SMW:** title artwork renders, with button symbols appearing as question marks in the captured title prompt. No recorded startup crash, but late FPS is about 12–14 with only 7% GPU-thread activity. The 30-second cold thread dump records main thread 80 repeatedly invoking SleepThread(-1), LR 0x8118e10. Investigate this guest caller, predicate and synchronization behavior before touching global waits. Texture-type mismatch counter is zero.
- **SMO:** start-mode menu renders near 60 FPS. The earlier zero-draw startup issue does not reproduce in these observation windows. Gameplay beyond the menu remains untested.
- **XC2:** cloud/title prompt renders at roughly 25 FPS; zero texture-type mismatches or failed shader compilations. Two shaders still contain three untranslated VMAD instructions each, so texture/shader correctness is incomplete. Late profiles show about 630 draws per frame and compute waits around 16 ms per frame. Prioritize instruction support and measured compute-readback synchronization; compare the same scene after each isolated change.
- **FFRS:** no frames, submissions, or draws during either window. The output window is black and the GPU thread is idle. Main thread 80 waits on a KThread; several workers wait on condition variables. These observations do not establish a file-read lock as the cause. Trace the joined worker and its completion predicate before changing VFS or condition-wait behavior.

## Native checks

Nine sanitizer-backed groups passed: shader dimensions (including GLSL validation when available), clock, IPC sessions, audio effects, audio routing, audio renderer updates, font adapters, heap adapters, and filesystem/concurrent 6 GiB archive reads. The remaining `service_ports` group does not compile because its harness references the missing `system_clock_context` helper. This is a test-suite gap; the full ten-group suite has not passed.

## Evidence

[Run metadata](games/runtime-validation-20261029/runs.json), [parsed measurements](games/runtime-validation-20261029/metrics.json), [service test failure](games/runtime-validation-20261029/service-ports-check.log). Logs, thread dumps, and screenshots below remain local generated data. No production source or settings changed during this test pass.

- Balatro: [cold log](games/runtime-validation-20261029/Balatro/cold.runtime.log), [cold threads](games/runtime-validation-20261029/Balatro/cold.threads.txt), [warm log](games/runtime-validation-20261029/Balatro/warm.runtime.log), [warm threads](games/runtime-validation-20261029/Balatro/warm.threads.txt), [warm screenshot](games/runtime-validation-20261029/Balatro/warm.png)
- Belmonts Curse: [cold log](games/runtime-validation-20261029/Belmonts%20Curse/cold.runtime.log), [cold threads](games/runtime-validation-20261029/Belmonts%20Curse/cold.threads.txt), [cold screenshot](games/runtime-validation-20261029/Belmonts%20Curse/cold.png), [warm log](games/runtime-validation-20261029/Belmonts%20Curse/warm.runtime.log), [warm threads](games/runtime-validation-20261029/Belmonts%20Curse/warm.threads.txt), [warm screenshot](games/runtime-validation-20261029/Belmonts%20Curse/warm.png)
- Bubble Bobble: [cold log](games/runtime-validation-20261029/Bubble%20Bobble/cold.runtime.log), [cold threads](games/runtime-validation-20261029/Bubble%20Bobble/cold.threads.txt), [cold screenshot](games/runtime-validation-20261029/Bubble%20Bobble/cold.png), [warm log](games/runtime-validation-20261029/Bubble%20Bobble/warm.runtime.log), [warm threads](games/runtime-validation-20261029/Bubble%20Bobble/warm.threads.txt), [warm screenshot](games/runtime-validation-20261029/Bubble%20Bobble/warm.png)
- FFRS: [cold log](games/runtime-validation-20261029/FFRS/cold.runtime.log), [cold threads](games/runtime-validation-20261029/FFRS/cold.threads.txt), [cold screenshot](games/runtime-validation-20261029/FFRS/cold.png), [warm log](games/runtime-validation-20261029/FFRS/warm.runtime.log), [warm threads](games/runtime-validation-20261029/FFRS/warm.threads.txt), [warm screenshot](games/runtime-validation-20261029/FFRS/warm.png)
- SMW: [cold log](games/runtime-validation-20261029/SMW/cold.runtime.log), [cold threads](games/runtime-validation-20261029/SMW/cold.threads.txt), [cold screenshot](games/runtime-validation-20261029/SMW/cold.png), [warm log](games/runtime-validation-20261029/SMW/warm.runtime.log), [warm threads](games/runtime-validation-20261029/SMW/warm.threads.txt), [warm screenshot](games/runtime-validation-20261029/SMW/warm.png)
- XC2: [cold log](games/runtime-validation-20261029/XC2/cold.runtime.log), [cold threads](games/runtime-validation-20261029/XC2/cold.threads.txt), [cold screenshot](games/runtime-validation-20261029/XC2/cold.png), [warm log](games/runtime-validation-20261029/XC2/warm.runtime.log), [warm threads](games/runtime-validation-20261029/XC2/warm.threads.txt), [warm screenshot](games/runtime-validation-20261029/XC2/warm.png)
- smo: [cold log](games/runtime-validation-20261029/smo/cold.runtime.log), [cold threads](games/runtime-validation-20261029/smo/cold.threads.txt), [cold screenshot](games/runtime-validation-20261029/smo/cold.png), [warm log](games/runtime-validation-20261029/smo/warm.runtime.log), [warm threads](games/runtime-validation-20261029/smo/warm.threads.txt), [warm screenshot](games/runtime-validation-20261029/smo/warm.png)
