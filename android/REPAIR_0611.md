# Android 0.6.11

Baseline: ebf65a683bd170b74eb17f4692a3f19f1ec50ed1 (0.6.10).

## Changes
- Replace the GLES blend-mode whitelist that disabled unsupported PSP modes. Map representable factors/equations to fixed-function GLES, including separate alpha factors. Emulate independent fixed colours, doubled factors, absolute differences and partial colour masks with integer fragment arithmetic.
- Prefer coherent EXT_shader_framebuffer_fetch when the driver advertises it and its shader compiles. Otherwise use an ordered GPU-copy fallback per triangle. Never read a texture while it is also the draw attachment; never approximate unsupported blend equations by disabling blending.
- Preserve overlapping triangle order in both paths. Do not remove legitimate green pixels with a colour filter.
- Disable automatically generated diagnostic material colours in ordinary gameplay. Missing textures are counted; a diagnostic colouring mode requires explicit opt-in.
- Add an exact per-primitive directional-light cache. Point/spot lights bypass the cache. No reduced-precision vertex or material arithmetic.
- Permit GPU clipping with disabled PSP depth clipping only when every vertex has positive W and lies in its original Z range. Reject unsafe inputs before consuming the draw; special viewport/depth modes retain the CPU fallback.
- Stamp native version and source SHA in both launcher and runtime/performance logs. Log GPU clip usage, blend equations, programmable blending and missing texture draws. The previous user log did not contain the 0.6.10 GPU clip counters, so it cannot independently identify the installed APK.
- Keep existing ATRAC3/ATRAC3+ support, continuous PCM queue, 1x resolution, single GLES owner and vblank pacing. Do not re-enable the formerly unstable full hardware-model-transform route.

## Verification
The actual GLES backend was run on Mesa llvmpipe, NOT on an Android/Mali device.
- 2178 rendered blend cases against the actual software reference, repeated with coherent framebuffer fetch and the ordered-copy fallback. This includes partial bit masks, min/max, subtraction, double factors and independent fixed colours. Native UNORM rounding tolerance is one byte value.
- Overlapped programmable-blend triangles; texture version/feedback tests; 120 clip/viewport image comparisons; flat shading, cull and depth tests.
- 48000 exact cached-versus-reference lighting comparisons and 2000 actual vertex-decoder integrations. Existing 5000 lighting/UV, 10000 clipping and 8000 bounded vertex-reader tests retained.
- Existing audio continuity and producer/consumer tests, including AddressSanitizer/UndefinedBehaviorSanitizer.
- The 0.6.10 backend fails the new blend reference test; this revision passes it. That demonstrates a renderer defect, not proof that every green/flickering car in the user's scene had this cause.

See CI artifacts for tests executed on the exact committed source and APK identity/ABI checks. A microbenchmark of recurring normals is host-only and is not an Android/game FPS result. Neither 60 sustained game FPS, all visual defects fixed, nor starvation-free audio under slow guest execution is certified. PMF movie playback remains excluded.
