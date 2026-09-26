# Android 0.6.14

Baseline: fffaaf08624128ad22c9303b1024d736975e9615 (0.6.13).

## Implemented changes
- Execute GE BBOX/BJUMP, restore OFFSET across nested CALL/RET, and handle ORIGIN at the address of its instruction. Invalid or unsupported bounding data uses a conservative visible fallback. Existing vertex/index stream advancement is retained. This fixes previously ignored control flow, not an unconditional FPS unlock.
- Conservatively reject a whole 3D primitive when every vertex is outside the same active clip plane. Crossing geometry, nonpositive W, clear operations and 2D mode retain the existing path. Log actual bboxTests/bboxOutside/bboxJumps/trivialRejects so device usage is measurable.
- Model PSP depth-buffer addresses separately from colour targets. Colour targets can share the same depth attachment, and switching away from a depth buffer no longer erases it. New colour attachments clear colour, not existing shared depth. Legacy descriptors and unusual size/stride aliases retain conservative limitations; arbitrary overlapping GPU-to-RAM depth aliasing is not implemented.
- Recycle CPU-side vertex/index vector allocations in a pool capped at 16 MiB and 2048 entries. Keep original ordering, snapshots, index bounds and separate strip boundaries. Validate contiguous non-indexed vertex streams once rather than once per record.
- Repair artificial effects-audio gaps when a nearly empty queue still contains playable PCM. Only an actually missed callback deadline triggers rebasing. Reserve the callback's output horizon, preserve immutable packets and add per-channel produced/mixed/deadline counters. ATRAC decoding, pitch, music and dialogue paths are unchanged.
- Keep selectable 1x/2x and the stored choice. No automatic resolution decrease; a 1x versus 2x comparison remains available. The old full hardware-model-transform path remains disabled.

## Verification
Regression tests use the actual GE interpreter/vertex reader, actual audio producer/callback (AAudio device mocked), and actual GLES backend on Mesa. CI retains the prior palette, mip, lighting, texture version, blend, clipping, geometry-buffer and ATRAC tests.

New tests verify nested CALL/RET and ORIGIN; BBOX/BJUMP with 80 inside/outside volumes and stream advancement; continuous near-empty effects alongside an unchanged music channel; actual deadline recovery; colour-target/depth-buffer independence and persistence; and exact rendering after 100 CPU batch-vector reuses. The 0.6.13 executor and audio code fail their new regression tests. The old GLES implementation is tested against the shared-depth test separately.

The supplied device log is about 19–21 successful presentations/second at 2x. No physical-device execution of 0.6.14 has occurred during preparation. These tests do not certify 60 unique complete game frames/second, complete removal of all green/flickering materials, or continuous sound when a guest persistently produces insufficient PCM. A low submission time is not a GPU timer-query measurement. Check the executed CI results rather than interpreting this document as a device benchmark.
