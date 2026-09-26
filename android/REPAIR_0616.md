# Android 0.6.16

Base: `10b02c913ce1f75428cd5585bd21c7568d6bb382` (published 0.6.14), plus the saved 0.6.15 source changes. Version code: 22.

## Changes

- Decode texture row pitch in the PSP's 16-byte units. Direct and indexed formats discard the low pitch bits and use at least one unit; compressed formats preserve their nonzero pixel pitch. Use the same effective pitch for CPU sampling, texture signatures and GPU mip descriptors. A reproduced eight-pixel indexed mip previously sampled green padding instead of its second row, and its signature missed updates to that row. A swizzled texture with an unaligned pitch also read the wrong eight-row block.
- Prepare the two environment-map light directions once per primitive. Vertex normals, skinning, morphing, lighting, UV arithmetic and color output retain the original calculations. This removes repeated square roots/divisions from reflective vertices without enabling the experimental full GPU transform path.
- Disable inherited face culling for the final presentation triangle. Previously a valid offscreen image could become a black window after a draw that selected the opposite winding.
- Recover the 0.6.15 framebuffer-texture extent fix, conservative early position rejection and manually requested render capture. See `REPAIR_0615.md` for their scope. Repair that revision's missing test-driver dependency and its outdated APK version checker.
- Build directly from the checked-in sources. CI no longer rewrites source files or commits a migration during a build. The APK checker derives its required version from Gradle and verifies the native source identity.

The row-pitch behavior was checked against PPSSPP's `GetTextureBufw` at [commit 46d5fae](https://github.com/hrydgard/ppsspp/blob/46d5fae34207fc1b793215da46f7c41ee7931590/GPU/Common/TextureDecoder.cpp). This change implements the register arithmetic; it does not import its decoder implementation.

## Verification

- `texture_stride_regression.cpp`: actual CPU texture decoder, green-padding reproduction, correct hash coverage, swizzled row blocks and format-dependent pitch. The earlier decoder fails these cases.
- `environment_map_regression.cpp`: 12,288 bit-exact comparisons across changing lights, invalid vectors, skinning, morphing, fog and UV modes. O2, O3 with floating-point contraction, and address/undefined sanitizers were exercised locally. A synthetic host run of 500,000 reflective vertices took a median 29.34 ms before preparation and 20.81 ms after it; this is not a game or Android FPS result.
- `gles_present_regression.cpp`: actual offscreen and window pixels after both culling directions. The earlier presentation code fails despite having correct offscreen pixels.
- `feedback_capture_backend.cpp`: actual framebuffer sampling with a different declared extent, GPU texture byte readback and capture validation.
- Existing lighting, geometry, palette, texture, GE control-flow, audio and GLES regressions remain in CI. Local LeakSanitizer process inspection is unavailable in this container; local sanitizer runs disable leak detection while retaining AddressSanitizer and UndefinedBehaviorSanitizer.

## Device validation still required

Synthetic texture errors are reproduced and corrected, but this does not establish that every green object in the reported scene has the same cause. No physical Samsung A15/Mali gameplay measurement has been performed. Full-rate gameplay, all vehicle materials and scene streaming must still be checked on the device.

To capture a remaining material problem, open Back > Capturar un fotograma in the affected scene, then export the completed ZIP from the launcher. Capture is opt-in and causes a one-off pause; do not use that frame to judge FPS. The inspector `android/tools/inspect_render_capture.py` reads the ZIP locally.

The existing application ID, touch controls, 1x/2x preference and audio implementation are retained. Signing compatibility depends on the build key: the previous CI used an ephemeral Android debug key, which is not in this repository. A locally signed build cannot replace it unless the original private key is available. Do not assume that uninstalling preserves internal app data.
