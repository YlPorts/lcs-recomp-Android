# 0.6.17 public verification evidence

This summary contains counts and timings only. The user-supplied ZIP, its extracted vertex records, game images and reference output binary are not included in the repository.

## Captured CPU stage

Reference renderer: unmodified `4bfcfee69084755a08150dc56ebd7fd210e230c4` (0.6.16), compiled separately with `-O3 -ffp-contract=fast`. The comparison driver is `capture_vertex_replay.cpp`; the input exporter is `../tools/capture_vertex_fixture.py`.

| Measurement | Reference | Updated |
| --- | ---: | ---: |
| Captured draws | 2,125 | 2,125 |
| Submitted vertices compared | 95,508 | 95,508 |
| Bit-exact vertex matches | Reference output | 95,508 |
| Full-record memo hits | 32,841 | 32,841 |
| Indexed-vertex hits | 197 | 197 |
| New ambient-only decoder calls | 0 | 43,124 |
| Output checksum | 700482461037500 | 700482461037500 |
| Host median CPU vertex replay, ms | 6.54828 | 4.74705 |
| Trial minimum, ms | 5.72185 | 4.38740 |
| Trial maximum, ms | 10.30080 | 6.47591 |

Timing uses seven trials of ten captured CPU vertex frames each. The bitwise comparison checks every decoded attribute separately; the checksum is an additional consistency value. These are host measurements of one scene's CPU vertex stage, not Android FPS or whole-frame timing. Replay follows observed state snapshots because original list boundaries and redundant state writes are absent from the capture.

The captured scene has 68,450 vertices in ambient-only layout `0x115`, 57 observed lighting-state changes and 1,625 observed vertex-state changes. The optimization applies only to its explicitly guarded format and lighting/UV state; it does not change the general lighting model.

## Self-contained regression

`ambient_vertex_regression.cpp`: **PASS**, 2,048 bit-exact comparisons. Individual light enablement, other layouts, generated UV modes, software rendering and malformed records retain the guarded fallback. The same test passed AddressSanitizer and UndefinedBehaviorSanitizer locally with leak detection disabled because the container cannot inspect processes for LeakSanitizer.

## GLES and capture archive

The supplied 0.6.16 ZIP is truncated at 16,001 entries and includes GPU batches 0–358 only. It lacks GPU readbacks for the later vehicle environment-map draws. An independent actual-decoder replay of 3,249 environment-map vertices found zero green-dominant vertex colours (all R ≥ G ≥ B); this is CPU colour evidence, not proof of correct GPU materials.

**Targeted GLES and allocation tests: PASS**, with framebuffer fetch enabled and with `PSPRECOMP_GLES_FB_FETCH=0`. `gles_vehicle_feedback_regression.cpp` observed RGB (0,255,0) on unmodified 0.6.16 where a red texel was expected; the baseline exits with status 20. The corrected capture-derived fixture verifies:

- Scene-to-64×64 RGB565 feedback; 50 distinct GPU consumer batches reuse one crop copy.
- Twenty-four edge comparisons: six UV samples × repeat/clamp × 1x/2x, compared with a tightly allocated reference texture.
- Source-write invalidation, ordered self-feedback and REGION2 bounds.
- Sampling against the actual 64-wide GL allocation even when a future 512-wide target is queued.
- Real GL cache pressure from five approximately 16 MiB sampling views: retained bytes ≤64 MiB and zero tracked bytes after destruction.

The implementation also snapshots GPU batch source stride/format/allocation and crop/match state at execution time for subsequent diagnostics.

**Combined existing GLES regressions: PASS**, with both framebuffer paths: the backend suite, including 2,178 blend-image comparisons per mode; shared-depth/recycled-geometry tests; presentation after either prior culling direction; and the declared-extent/GPU readback regression. Independent GPU capture ZIP verification passed CRC checks, exact 16-byte texture contents, six 36-byte vertex records, metadata and an untruncated status. Actual shader linking/pixel readback smoke tests passed on llvmpipe LLVM 20.1.2. These are host Mesa results, not physical Android FPS measurements.

The extent fixture now explicitly selects the source framebuffer's RGBA format instead of its unintended default RGB565 format. This fixture is not a complete game GPU-frame replay; CPU-decoded images alone do not validate GPU sampling.

**Capture archive: PASS.** Explicit requests, frame boundaries, concurrent rejection, binary/empty entries, 64 MiB payload bounds, 32,000-entry bounds, a complete 22,000-entry dense frame, CRC/content checks and truncation markers were verified. `scale_config_regression.py` and `capture_ui_contract.py` also passed with version 0.6.17.
