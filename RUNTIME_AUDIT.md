> Current status: see [SESSION_HANDOFF.md](SESSION_HANDOFF.md). This report retains historical milestones; older runtime markers, hashes, test passes, and failure states do not describe every later source revision.

# Runtime structural audit — 2026-10-29

Reviewed the current [structural manifest](.switchrecomp-graphics-rules.md) against the first-party kernel, HLE/IPC, filesystem, platform timers, GPU engines, OpenGL presentation/shader/buffer paths, and existing regression tests. This is a targeted architectural audit, not a claim that every source line, bundled dependency, or backend has been verified. Extracted assets, packed archives, captures, and third-party decoder sources were excluded from indexing.

## Repairs

| Area | Finding and change |
| --- | --- |
| Archive descriptor cache | Cache misses opened/closed files while holding the metadata mutex. Open now occurs outside the lock, followed by a locked duplicate check and installation. Eviction closes the retired descriptor after unlocking. Reference counts continue to protect in-flight reads. |
| RomFS read completion | Short host reads could be treated as success or converted into zero-filled data; large counts also passed through signed `int` comparisons. A bounded positional loop now requires complete reads and propagates failures through IStorage. Guest-range and subtraction-based size checks avoid overflow. |
| Recursive filesystem copy | A static scratch array could be written by concurrent copy operations. The buffer is now local to each invocation. |
| Missing-path polling | GetEntryType now uses a thread-local negative cache: 16 entries, 250 ms expiry, and atomic invalidation on filesystem mutation. It caches only ENOENT/ENOTDIR and adds no global read lock. |
| Kernel priority updates | SetThreadPriority accepted arbitrary values even though CreateThread validates 0–63. Updates now reject out-of-range values. The generic retry brake uses the portable sleep function on both platforms. |
| Presentation fallback | The window presentation shader samples sampler2D, while the fallback candidate set allowed array/cube targets. Both address and dimension sweeps now require a real compatible 2D color texture. |

## Existing safeguards verified by inspection

- [Kernel](runtime/kernel.c): populated thread names, bounded default cores and affinity masks, 32/64-tick sleep throttles, and 64-poll yields. Throttles do not force an unlock from SleepThread. Condition-wait expiry reacquires the guest mutex before returning.
- [IPC](runtime/ipc.c): service objects serialize into registered session handles or domain IDs; ownership and clone/domain lifecycle checks exist.
- [Audio services](runtime/svc_core.c): renderer configuration preserves application capacities, including SMO's 96 voices and 14 effects. SDK getters return their defined fields. Adding those counts to unrelated payloads would change the wire ABI.
- [Clock adapter](runtime/hle_services.c) and [platform clock](runtime/plat.c): bounded transient realtime retries, initialized timespecs, and original-handler fallback for unsupported queries, invalid outputs, or permanent host failures.
- [Filesystem](runtime/svc_fs.c): file and archive reads use explicit offsets; descriptor references prevent cache eviction during reads.
- [GPU engines](runtime/gpu_engines.c): DMA checks already bound block geometry, row pitches, extents, and allocation sizes before copies.
- [OpenGL](runtime/gpu_gl.c): separate texture/render-target format decoders, committed-framebuffer gating for shader specialization, packed attribute formats, bounded buffer-cache pruning, and uniform-store generation invalidation after orphaning. Shader paths check compile/link results. Existing backend-specific crash workarounds were retained.

## Validation and limits

The nine native test groups pass. Filesystem coverage now includes seven concurrent file/cache readers beyond 6 GiB, unchanged stream cursor, write/read coherence, short-read failure, storage error propagation, negative-cache expiry, and mutation invalidation. The Linux X11 build passes; Windows/Vulkan-specific execution was not tested.

Isolated game smoke results are recorded below when testing finishes. They use separate save/config/cache locations and preserve regular launch logs. Short smoke tests cannot establish gameplay correctness or demonstrate a performance improvement.

## Remaining audit findings

- FFRS and SMO had unresolved startup stalls in prior tests; SMW advanced to rendering before a later stall. No assumption that filesystem contention alone caused these stalls is justified.
- SSL context creation returns a tracked object, but its subcommands remain incomplete. Invented profile tokens would not provide a working TLS implementation.
- Service buffer validation and empty-success fallbacks remain inconsistent across older HLE handlers and need command-specific wire layouts and focused tests before blanket changes.
- Kernel handle lookups and diagnostic rings have broader lifetime/concurrency concerns: some lookup APIs return borrowed pointers after dropping the handle-table lock, and diagnostic producers/readers do not use a complete record-publication protocol. These require coordinated ownership changes, not an extra yield or fake-success return.
- The manifest's historical association of nearest C++ symbol labels with array/RTTI failures was disproven for Belmont: its captured exception was a realtime-clock system_error. Likewise, addresses below the heap base can be valid module mappings; heap-base comparisons alone are not memory-validity checks.
- Arbitrary placeholder textures, fabricated audio handles, unconditional clock success, and disabling cache invalidation were not introduced. These require valid backing storage, correct IPC ownership, initialized outputs, and evidence that guest memory is unchanged.

## Isolated smoke results

Each game ran for 12 seconds, with frame counts from its final periodic GPU summary. No fatal error was recorded in these runs.

| Game | Observed result |
| --- | --- |
| Balatro | 378 frames; presentation continued. |
| Bubble Bobble | 541 frames; presentation continued. |
| Belmonts Curse | 509 frames; presentation continued without the previous clock exception. |
| SMW | Reached 200 frames, then stopped advancing; stall remains. |
| smo | Zero frames, one GPU submission; startup stall remains. |
| FFRS | Zero frames and zero GPU submissions; startup stall remains. |

Logs are `games/<game>.audit-smoke.log`. The rebuilt Linux X11 runtime is staged for all six games, with `.pre-runtime-audit` rollback copies. SHA256: `dda2c7968f3f2a654863fb90e5419b8fe1a2e6212189565dbf8477ebadc40583`.


## Descriptor-validated sampler variants

Fragment shader selection now inspects the active TIC and resolved resource target. Compatible TEX/TLD/TEXS/TLDS slots bound to TIC type 2 volumes select a separately cached 3D shader variant. The translator changes both the sampler declaration and coordinate expression (vec3 for sampling, ivec3 for integer fetches), using the respective instruction register layouts. Original opcode types retain stable slot IDs. Shadow, gather, array, mixed incompatible uses, and packed-offset forms are excluded. The override mask is retained in shader metadata and checked in both address and content caches.

Tests in scripts/test_shader_dimensions.c cover 2D/3D ordinary and packed TEX/TLD output, repeated slot stability, and mixed-operation exclusion. Generated fragment shaders pass glslangValidator; sanitizer checks and the Linux X11 build pass. The broader existing service test remains incompatible with the current service source (missing system_clock_context helper); no full-suite pass is claimed.

An isolated 18-second XC2 Vulkan-output run reached 413 frames and 180,568 draw submissions at 16.71 seconds, with type-mismatch=0 and no recorded shader compilation/link failure. This does not establish full gameplay or visual correctness. See [XC2.sampler-smoke.log](games/XC2.sampler-smoke.log). Regular logs/saves were preserved.

Runtime staged for all seven games; rollback suffix `.pre-sampler-dimensions`. SHA256: `a7b16e1da7085e9adcb9ce502ad6e4337e8c18428e90666873951c34ccb56be1`.

## Audio completion notification — SMO startup progress

The RequestUpdate renderer-info counter now advances by completed 5 ms quanta rather than IPC calls, and remains unchanged while stopped. Voice sample positions continue to come from actual mixer consumption. REV3's output structure is retained; no unsupported renderer-info section is appended.

After a successful running update, all status/timeline bytes are written before signaling the retained audio-renderer synchronization event. Catch-up scheduling remains separate from this completion notification. Invalid updates and stopped renderers do not generate a running-update completion signal.

Targeted sanitized RequestUpdate tests verify one-quantum progress, three-quantum catch-up, stopped state, and output-before-notification ordering. Shader-dimension generation tests and GLSL validation also pass. The broader existing service-test/source mismatch is not repaired by this change, so no full-suite pass is claimed.

An isolated 15-second SMO run reached 508 frames and 3,725 draw submissions at 11.66 seconds, compared with zero draws/frames in earlier startup tests. An isolated XC2 run reached 259 frames at 11.63 seconds with type-mismatch=0. No fatal error was recorded in either run. Logs: [smo.audio-timeline-smoke.log](games/smo.audio-timeline-smoke.log), [XC2.audio-timeline-smoke.log](games/XC2.audio-timeline-smoke.log). These observations supersede the earlier SMO zero-frame result but do not establish full gameplay/audio correctness.

The final rebuild identifies itself as `v195-audio-completion`; only the version marker changed after these functional smoke tests.

Final binary SHA256: `ee7285add01fdbb74b613e422385cbfbef8f4e37bda0964c0465f5aa99ef0d93`. Staged for seven games; rollback copies use `.pre-audio-completion`.
