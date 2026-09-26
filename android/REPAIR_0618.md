# Android 0.6.18

Base: `e456abb5da7983079c4e3270d1cfaa1fea97368b` (0.6.17). Version code: 24.

## Reflection rendering and screen edges

The new complete device capture exposes a failure before GLES receives the reflection geometry. Android's screen-centered HUD adjustment also moved offscreen through-mode primitives. The 64×64 scene-copy pass, originally at X=0–64, reached the GPU at X=66.95–114.22 and could not draw inside its target. Its highlight passes were outside the target too. The source texture readbacks match the decoded images; changing palettes or suppressing green channels would not repair this coordinate error.

The HUD adjustment now applies only to display-sized render targets. It recognizes backbuffers by their layout rather than requiring the current display address, preserving double buffering. Reflection maps, framebuffer-copy sprites and world effects that read scene depth keep their original coordinates. Fullscreen images and narrow HUD scissor rectangles retain their intended behavior. The regression exercises `render_ge_primitive` and actual GLES pixels, including the intervening Android transform that a backend-only test misses. It passes with both framebuffer-fetch paths; the unmodified 0.6.17 renderer reproduces the exact captured displacement and exits with the expected status 31.

The guest's existing widescreen camera and visibility hooks now use the physical Android surface aspect instead of the internal render resolution. In the supplied case that means 1536×709 rather than a 16:9 camera followed by a late geometric stretch. The 3D render, early rejection and bounding-box paths share the same correction; they do not widen the guest projection twice. The independent screen-space HUD correction remains available. Changing render scale between 1x and 2x does not change the camera aspect.

The camera regression checks the actual aspect/extent hooks and both side edges, preserves the 512×320 viewport, and covers missing dimensions, transient portrait dimensions, native 16:9, explicit aspect settings and disabled widescreen hooks. The unmodified 0.6.17 code fails with camera aspect 1.77778 versus physical aspect 2.16643. Optimized and AddressSanitizer/UndefinedBehaviorSanitizer runs pass; local LeakSanitizer process inspection is unavailable in the test container.

See `tests/evidence_0618.md` for the numeric capture findings. Raw game images and captured geometry are not repository assets.

## Texture recovery during a surface gap

An ordinary Android surface replacement preserves the EGL context and previously uploaded texture images. However, the previous upload path discarded newly decoded pixels when no native window was attached. A real EGL/GLES reproduction retained an older red texture but lost a newly decoded blue texture: the first resumed draw was white and incremented `missingTextures`; uploading the image again repaired it.

Decoded images now wait in a bounded queue until a current graphics context is available. The queue preserves known content versions at a shared guest address and drains before resumed drawing. It is limited to 32 MiB of image data and 1,024 entries. A rejected queued upload makes that GE frame incomplete; it is discarded before drawing or swapping, retaining the previous image until a complete frame can be rendered. A failed GL upload does not expose a partial image as a cache hit, and its decoded bytes remain available for retry. Shutdown releases deferred data even when no surface is attached.

`gles_surface_texture_regression.cpp` checks the first resumed presentation with 129 deferred textures, queued A/B/A versions of one address, existing-context/image preservation, an injected GL upload failure and automatic retry, both queue limits, unchanged framebuffer pixels after an incomplete frame, subsequent retry and detached shutdown. Zero-signature images retain the pre-existing unknown-version lookup semantics; the queue does not let their upload overwrite a separately keyed known version.

The user's 129 missing-texture draws span a 5.339-second reporting interval. The adjacent `textureMs=19.931` belongs to a separate multi-frame average. Those counters do not identify the exact cause of every missing draw or quantify a single loading stall. The separately observed metadata-only framebuffer readiness case is not changed by this recovery fix, since queued framebuffer producers must remain valid.

## Performance and validation limits

Local validation passes: camera and offscreen-reflection regressions (including failing 0.6.17 controls), surface recovery with AddressSanitizer/UndefinedBehaviorSanitizer, existing GLES backend/depth/presentation/feedback tests in both framebuffer-fetch modes, feedback ZIP byte checks, shader validation, package/version checks, and Android NDK ARM64 syntax compilation. The surface recovery changes also passed an independent code review and regression run. These are host-side Mesa tests and compiler checks, not a physical Android benchmark or an APK build. LeakSanitizer was disabled for the local Mesa sanitizer run.

The supplied log includes different workloads. Its 55-FPS interval has very little game geometry; the later gameplay interval reports approximately 19 FPS at 2x. Neither is a measurement of 0.6.18. Correcting the guest visibility frustum may submit additional previously missing objects, so the camera correction is not an FPS optimization claim.

The reflection regression establishes that the previously clipped generation passes now reach their target. The saved capture frame has mostly gray reflections, while the stronger green flicker appears in the separate screenshots. This does not prove every material artifact or gameplay hitch is resolved. Physical device validation is still required.

The existing package IDs, signing configuration and render-scale preference are retained. Publish the source and trigger the existing Android workflow; no local APK build or wait for GitHub compilation is required for this revision's delivery.
