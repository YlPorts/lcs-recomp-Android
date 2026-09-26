# Android 0.6.15 — local, unpublished revision

Baseline: 10b02c913ce1f75428cd5585bd21c7568d6bb382 (0.6.14).

## Implemented

- Correct normalized UV scaling when a PSP texture samples an existing GPU framebuffer with a different declared texture extent. Apply the declared/logical size ratio before flipping V. Ordinary decoded textures are unchanged. Unknown legacy dimensions keep the old mapping. Explicitly different row strides retain the old path: arbitrary overlapping VRAM/address remapping is NOT implemented here.
- Add a conservative position-only pretest before lighting and texture uploads, on Android's GPU-only 3D path. It rejects only a whole primitive outside one common active plane. Skinning, morphs, 2D, clear operations, invalid input and nonpositive W fall back. Existing full processing remains for visible/crossing/uncertain geometry. `PSPRECOMP_GE_EARLY_POSITION_CULL=0` disables the pretest; its rejections contribute to the existing `trivialRejects` counter.
- Add explicit, opt-in capture of the next nonempty GE frame. It records command registers, transform matrices, loaded palettes, raw vertex/index records, CPU-decoded texture images, GPU batch vertices/selected numeric state, actual GPU-cache texture readbacks and a final framebuffer image. It is a diagnostic bundle, NOT a complete standalone PSP replay or a ROM/save dump. Intermediate framebuffer/depth contents and unsupported aliasing are not fully reconstructed.
- The capture ZIP has a 64 MiB payload budget and 16000 normal entries. The manifest explicitly marks truncated/incomplete captures. Failed captures do not replace the last completed ZIP. Only the native game/GE owner writes it; the UI can request/poll/export. No automatic network transfer. No padded native descriptor structures are serialized.
- Add Back -> Capture a frame; export the completed ZIP from the launcher via Android's file picker, outside gameplay. This avoids opening the file picker while the active SurfaceView is in use. Capturing causes an intentional one-off stall and MUST NOT be used for FPS measurement.
- Include a standard-library-only ZIP inspector producing a local HTML contact sheet. Its green-fraction ranking is only a search aid: legitimate green objects are not altered or declared erroneous.
- Preserve the existing 1x/2x selection, game/vblank timing and all audio/ATRAC implementation files. Do not enable the previously unstable full model-transform GPU path.

## Local validation actually executed

- 6000 pretest decisions against the actual full CPU vertex decoder; indexed, malformed, clear, skin, morph and 2D fallbacks. Also executed with AddressSanitizer and UndefinedBehaviorSanitizer.
- ZIP request/lifecycle/empty-vblank/cancellation/budget/I/O tests, including sanitizers. Python independently verified CRCs, exact bytes, uniqueness and truncation flags.
- Actual GLES shaders: eight feedback-extent readback comparisons and unchanged ordinary-texture sampling. The 0.6.14 shader fails the new nontrivial extent test, as expected.
- Actual GLES backend: copy a complete 16x16 framebuffer through a texture declared 32x32 with the same row stride; the copied image matches exactly. Actual cached GPU texture bytes were read into the diagnostic ZIP and independently checked against a known four-colour input. Framebuffer binding was restored.
- Existing GLES backend smoke tests ran in coherent-fetch and ordered-copy modes. Existing audio continuity tests passed unchanged.
- Native C++ Android conditional paths were compiled on the host. UI/JNI wiring and the version/scale contract were statically checked. The workflow YAML was parsed.

These tests used desktop Mesa llvmpipe, with host compiler/API declarations, NOT Android NDK libraries or a physical Mali GPU. No Android SDK/NDK was available for an APK build in this session. Java/Android compilation and the full CI matrix remain pending. The supplied workflow preserves the existing tests and adds the new ones for a future build.

## What is NOT established

No physical-device FPS improvement, no 60-FPS result, no proven complete repair of green vehicle materials, no complete scenery-streaming fix. The framebuffer defect above is reproduced in a synthetic test; that does not identify the cause of every green object in the user's screenshots. Capture the affected scene to compare the actual GPU images, vertex colours and UVs instead of assuming a palette or colour-key cause.
