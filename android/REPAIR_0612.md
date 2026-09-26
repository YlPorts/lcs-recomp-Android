# Android 0.6.12

Baseline: 2bd2ce61a3e155595c1eeb131cccd41b287aa6bb (0.6.11).

## Changes
- A persistent launcher 1x/2x toggle feeds both JNI and the render configuration. Default 2x for this requested comparison; returning to 1x is supported. This is internal target resolution, not just display scaling. Doubling each axis produces four times the pixel count and can lower FPS. The logged `render=` reports actual target size (which can differ from 480x272).
- Execute texture matrix commands 0x40/0x41. The previous interpreter gated the transform update at 0x3F even though the transform helper supported 0x40/0x41. Repeated identical matrix data words must still advance the cursor.
- Implement an immutable, 1024-byte internal CLUT snapshot on CLUTLOAD. Partial loads replace only the requested 32-byte blocks; zero-length loads do not erase the palette. Texture decoding and queued software draws retain the correct snapshot across later RAM writes. Cache identity includes the entire loaded palette, including upper 16-bit entries.
- Merge adjacent compatible GPU clip-space lists, strips and fans using explicit triangle indices. Preserve primitive boundaries, winding, provoking vertex, draw order, texture versions and framebuffer feedback barriers. Do not sort by texture or join independent strips directly.
- Exact per-primitive reuse of repeated non-indexed vertex records, limited to costly lighting/skinning/morph cases and records up to 64 bytes. Full byte comparisons reject hash collisions and changes; primitive boundaries invalidate the cache. No reduced precision or cross-frame reuse.
- Add `mergedClip`, `renderScale`, `render`, `vertexMemoHits`, `clutLoads`, `texMatrixWords` and `paletteDraws` counters.
- Keep the existing audio queue and ATRAC decoders, single GLES owner, vblank pacing and safe CPU fallbacks. No new claims of PMF support.

## Regression coverage
- Execute real GE lists through the command interpreter and inspect actual vertex/texture reader state. Verify texture matrix loads and CLUT lifetime with RAM overwrite, partial/zero loads, T4/T8/T16/T32 and upper 16-bit entries. The 0.6.11 executor fails the two new state tests.
- 61,440 exact full-vertex comparisons with repeated/mutated records and changing primitive/material state; malformed record and failed decode behaviour. 300 index-conversion cases preserve boundaries and provoking vertices.
- Actual GLES backend comparisons of merged lists/strips/fans against native GL primitive assembly, including flat shading, culling and alpha overlap. Actual 1x -> 2x -> 1x target-size/readback tests. Both coherent framebuffer fetch and ordered-copy paths run in CI on Mesa.
- Existing component/audio, geometry, bounded vertex reader, lighting, shader, blending and real ATRAC decoding tests retained. New state and memo tests also run with AddressSanitizer/UndefinedBehaviorSanitizer.

See artifacts for executed test outputs, not just this test plan. Host microbenchmarks are not Android or game FPS measurements. There is no physical-device result for this revision yet. Neither 60 sustained FPS nor complete elimination of green/flickering materials is certified. A GPU-rendered palette source that requires GPU-to-guest readback is not added by this CLUT change.
