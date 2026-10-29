# Targeted runtime repairs — 2026-10-29

Runtime: `v198-sparse-uploads-vmad-fonts`. SHA-256: `5d6f0a99b6b21cebd29cba0f0b760b4d1b896345016d972e696e82c2c0b48834`.

## Implemented

- [Vertex upload cache](runtime/gpu_gl.c): remove the coarse changed-block threshold that promoted scattered changes to full uploads. Skip equal 64 KiB blocks, compare stride-aligned vertex rows within dirty blocks, coalesce adjacent changed rows, and submit only those ranges. Unknown strides compare at byte granularity. Preserve GL buffer identity and the exact mirror, with initial allocation and mirror-budget fallback retained. Dense actual changes still require full-range uploads.
- [Font fallback](runtime/main.c), [Unicode map adapter](runtime/font_buttons.h): fallback system fonts acquire missing Switch private-use button mappings to readable existing letters, arrows, plus/minus and symbols. Unicode format 4 and 12 maps and checksums are rebuilt; original glyphs, metrics, existing private glyphs and supplementary mappings remain. User-provided console fonts are untouched. These are fallback labels, not replicas of proprietary button artwork; ZL/ZR and SL/SR use L/R labels. The SMW screenshot now shows L + R instead of question marks. The repeated SleepThread caller remains.
- [Shader translator](runtime/shader.c): integer VMAD byte/halfword/word extraction, signed/unsigned extension, register/immediate operands, wrapping multiply and add. Existing predication and zero-register behavior remain. Scale, negation, saturation and condition-code modifiers without verified semantics remain explicit untranslated diagnostics: this is not complete support for every VMAD encoding. The observed XC2 VMAD instructions translate; two ISBERD instructions remain untranslated in each affected shader.

## Condition waits

The requested global zero-key bypass was **not reapplied**. It previously broke nearly every game. A zero condition cell does not mean waiting is unnecessary. The existing atomic guest-mutex release, waiter registration and mutex reacquisition path remains in [kernel](runtime/kernel.c). Wait is SVC 0x1C; 0x1D signals. FFRS still waits before any GPU submission. No verified lost-wake repair was established; no speculative alternate synchronization change was applied.

## Verification

- Sanitized actual-uploader harness: sparse dirty rows across every coarse block, adjacent rows, partial tails, byte-granularity unknown strides, repeated edits, unchanged data, and exact GPU-mock/mirror agreement.
- Font checks: synthetic malformed/truncated containers, pre-existing private glyphs, BMP and supplementary Unicode preservation, font checksums. FontTools independently validated the real patched DejaVu cmap and all 5,918 original mappings.
- Host OpenGL execution: 288 generated VMAD arithmetic cases match the CPU reference, with unsupported modifier forms diagnosed. Driver-internal leak checking disabled for this GPU test only.
- Ten compatible native groups passed, including the new font-button test; the separate buffer test also passed. The service_ports harness still fails to compile against its missing system_clock_context helper; no full-suite pass is claimed.
- Linux X11 build passed. Seven sequential isolated game runs used copied warm application shader caches and original-game settings/save copies, Vulkan/Xlib output, screenshots and 30-second measurement windows. Host cache state and scene progress are uncontrolled. The FFRS harness was interrupted and its remaining child was manually stopped after 30 seconds; analysis uses only samples before 30 seconds, while its last screenshot may be later. Process signal exits are harness shutdowns.

## Game observations

Last sampled FPS before 30 seconds; values are exploratory and not controlled same-scene speedups.

| Game | Previous warm FPS | New FPS | Frames / draws | Texture mismatch / failed shader |
|---|---:|---:|---|---|
| Balatro | 4.2 | 3.5 | 408 / 7579 | 0 / 0 |
| Belmonts Curse | 58.4 | 60.0 | 1380 / 48559 | 0 / 0 |
| Bubble Bobble | 60.0 | 60.0 | 1439 / 9848 | 0 / 0 |
| FFRS | 0.0 | 0.0 | 0 / 0 | 0 / 0 |
| SMW | 14.2 | 19.8 | 578 / 22305 | 0 / 0 |
| XC2 | 25.4 | 24.7 | 566 / 274844 | 0 / 0 |
| smo | 60.0 | 60.0 | 1432 / 32356 | 0 / 0 |

Balatro still shows dense late-startup uploads and a severe slowdown; no overall performance gain is established. SMW button labels are repaired but its sleep loop persists. XC2 VMAD diagnostics are eliminated for the observed forms without an established FPS or compute-wait improvement. FFRS remains black with zero submissions. Full gameplay and texture correctness remain unverified.

## Reproduce focused checks

```sh
python3 scripts/test_buffer_cache.py
gcc -fsanitize=address,undefined -g scripts/test_font_buttons.c -o /tmp/swr_font_buttons
/tmp/swr_font_buttons
gcc -I runtime -fsanitize=address,undefined -g scripts/test_shader_vmad.c -lEGL -lGL -lm -o /tmp/swr_test_vmad
ASAN_OPTIONS=detect_leaks=0 /tmp/swr_test_vmad
```

## References and evidence

VMAD encoding/operand behavior was cross-checked against [the Maxwell translator source](https://github.com/eden-emulator/mirror/blob/master/src/shader_recompiler/frontend/maxwell/translate/impl/video_multiply_add.cpp). Private-use control mappings were checked against the [NXJS Switch icon definitions](https://gist.github.com/LauraWebdev/f73aab5b1ff2b2f9136d131b1b020907). These references do not establish the causes of the game stalls.

[Run metadata](games/runtime-repairs-20261029/runs.json), [parsed metrics](games/runtime-repairs-20261029/metrics.json), [previous baseline](RUNTIME_VALIDATION.md).

- Balatro: [log](games/runtime-repairs-20261029/Balatro/patched.runtime.log), [threads](games/runtime-repairs-20261029/Balatro/patched.threads.txt), [screenshot](games/runtime-repairs-20261029/Balatro/patched.png)
- Belmonts Curse: [log](games/runtime-repairs-20261029/Belmonts%20Curse/patched.runtime.log), [threads](games/runtime-repairs-20261029/Belmonts%20Curse/patched.threads.txt), [screenshot](games/runtime-repairs-20261029/Belmonts%20Curse/patched.png)
- Bubble Bobble: [log](games/runtime-repairs-20261029/Bubble%20Bobble/patched.runtime.log), [threads](games/runtime-repairs-20261029/Bubble%20Bobble/patched.threads.txt), [screenshot](games/runtime-repairs-20261029/Bubble%20Bobble/patched.png)
- FFRS: [log](games/runtime-repairs-20261029/FFRS/patched.runtime.log), [threads](games/runtime-repairs-20261029/FFRS/patched.threads.txt), [screenshot](games/runtime-repairs-20261029/FFRS/patched.png)
- SMW: [log](games/runtime-repairs-20261029/SMW/patched.runtime.log), [threads](games/runtime-repairs-20261029/SMW/patched.threads.txt), [screenshot](games/runtime-repairs-20261029/SMW/patched.png)
- XC2: [log](games/runtime-repairs-20261029/XC2/patched.runtime.log), [threads](games/runtime-repairs-20261029/XC2/patched.threads.txt), [screenshot](games/runtime-repairs-20261029/XC2/patched.png)
- smo: [log](games/runtime-repairs-20261029/smo/patched.runtime.log), [threads](games/runtime-repairs-20261029/smo/patched.threads.txt), [screenshot](games/runtime-repairs-20261029/smo/patched.png)
