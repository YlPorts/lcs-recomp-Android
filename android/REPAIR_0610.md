# Android 0.6.10

Baseline: c37d6d4c (0.6.9).

## Implemented
- Resolve the complete vertex record once, then decode from a validated byte view. The same lighting, morphing, skinning and UV algorithms are retained.
- Optional GLES homogeneous clip-space submission for standard PSP viewport/depth modes. CPU still computes model transforms and lighting; GLES clips, performs perspective division and assembles triangle lists/strips/fans. Unusual modes retain the previous CPU fallback.
- Preserve flat provoking-vertex colour and winding. Never concatenate strips/fans across primitive boundaries. Keep draw order, texture snapshots and 2D behaviour.
- Add gpuClipDraws/gpuClipVertices measurements. Maintain per-vblank timing, 1x internal resolution and the previously stable single GLES owner thread.
- Replace the always-failing Android media stub with the actual FFmpeg-backed AudioStreamDecoder, linked to replaceable ARM64 shared FFmpeg libraries. ATRAC3/ATRAC3+ streams can now produce PCM.
- Pin FFmpeg 7.1.5 to 3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587. Include LGPL notices, full upstream source and build configuration with the artifacts.
- Log opened stream codec/rate/channels and failures. The separate PMF movie path is still skipped until its EGL video-upload path is verified.

## Tests
CI runs the existing regressions plus 8,000 decoder comparisons, truncated-record rejection with sanitizers, actual GLES clip/viewport image comparisons, flat-shading/cull/depth tests, and real ATRAC3/ATRAC3+ sample decoding using the application decoder. Test media are temporary CI inputs and are not bundled.

These are component and synthetic renderer tests, not a physical Samsung A15 gameplay test. No 60-FPS result or complete elimination of every visual defect is certified. The existing audio queue cannot supply sound that a persistently slow guest has not produced.
