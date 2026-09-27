# Android 0.6.19

Base: `1c231b5128f95694f891d99d4912c08375ad25ad` (0.6.18). Version code: 25.

## Reported workload

The new phone log reports approximately 9–14 presented FPS at 2x. In its active rendering intervals, GE processing averages about 43–59 ms per frame call, including 18–24 ms decoding vertices and 10–14 ms in the phase labelled `clipMs`. Submission takes another 17–35 ms. The clip phase includes conversion and accumulation for GLES homogeneous clipping; it does not establish that the software polygon clipper dominates.

These intervals have zero missing textures and effectively zero texture decode/upload time. Approximately 1,100 GPU draws and 105,000–109,000 submitted clip vertices per presented frame remain. Geometry buffer fence waits are only about 0.006 ms per frame. Feedback crops occur about once every two frames. Neither counter supports blaming repeated texture decoding or geometry fence waits for the sustained slowdown.

The final interval has almost no GPU submission and is not treated as representative of active presentation. Android reports a removed task and no recorded signal. The timing data alone cannot distinguish CPU frequency changes or operating-system scheduling from changes in the scene workload.

## Changes

The established ambient `0x115` vertex path now gives the existing memoizer its known 10-byte record size at compile time. This lets the compiler unroll the same hash, full-record comparison and copy. It does not alter the hash, cache size, hit decisions, state invalidation or floating-point transforms. Unexpected record sizes use the checked generic path. A separately tested position-result cache was slower and was not retained.

Android clip vertices and retained batches now use the 36-byte layout already consumed by GLES and written to GPU captures, instead of retaining 56-byte desktop vertex records and repacking them at frame finish. UV normalization is combined with appending the batch. Position, color, UV, projective Q and fog keep their original precision; vertex counts, indices, winding and ordering remain unchanged. Intermediate vertex storage is 35.7% smaller. The actual GPU upload format was already 36 bytes, so this is not a claim of 35.7% less GPU upload traffic.

Pixel shader uniforms are updated individually when their cached values change. Previously a change to one material property resent the whole group of 21 uniform calls. Program destruction still invalidates the complete cache. This avoids redundant driver calls without changing shaders or merging additional draws.

## Checks and measurements

The new fixed-size memo regression compares 36,864 real decoder results bit for bit, including color and every float field, and checks identical hits/misses under mutations, collisions, changed state/layout, unknown revisions, failed decodes and short/null inputs.

Replay of all 61,479 captured vertices compares every result bit for bit against the original 0.6.18 decoder output, with unchanged memo/index reuse counts. Seven alternating host A/B pairs at `-O3 -ffp-contract=fast` give medians of 2.85178 versus 2.70085 ms for the vertex replay (5.29% less time). Android already uses those optimization flags for the renderer. At `-O2`, 2.96414 versus 2.97213 ms is effectively unchanged. These compiler/host-specific results do not establish the improvement on ARM or the complete game frame. The new CPU regression also passes AddressSanitizer/UndefinedBehaviorSanitizer.

The GLES submission regression compares 120 actual framebuffer images and all 21 shader uniform readbacks between forced full updates and incremental updates. It verifies multiple distinct images, so fragment discards cannot mask all changes. The controlled setter sequence uses 2,520 versus 120 uniform calls. It also compares legacy and compact input APIs byte for byte, including triangle lists, strips, fans, flat shading, merge boundaries and depth rejection. Existing capture export remains a 36-byte vertex record.

The available complete device capture is from 0.6.17, not the new 0.6.18 session; only the latest session's log was supplied. Reconstructing uniform state across its 398 captured GPU batches predicts 4,263 versus 331 pixel-uniform calls (92.2% fewer). This is a call-count estimate from that frame, not a measurement of phone frame rate. Paired host runs of clip conversion, actual batch accumulation and final packing produce the same 2,616,408 bytes, with a modest approximately 3.4% median reduction in that stage. Backend-only timing is within noise. No large gameplay speedup is inferred from these microbenchmarks.

Local validation passes for all eight GLES harnesses (backend, depth, presentation, feedback capture, vehicle reflections, surface recovery, offscreen HUD/reflections and submission) with both framebuffer-fetch settings, exact capture ZIP checks, shader checks, and the strengthened submission test under AddressSanitizer/UndefinedBehaviorSanitizer. The renderer and GLES backend pass Android NDK ARM64 syntax compilation. Independent review and repeat submission/lifecycle tests found no blockers. LeakSanitizer was disabled for local sanitizer runs. Version/scale/UI contracts and workflow YAML/shell syntax also pass. No local APK build or physical device benchmark was performed.

See `tests/evidence_0619_cpu.md` for reproducible CPU replay commands and exact comparison results. Raw captured game assets are not included in the repository.

## Preserved behavior

The physical-aspect camera/frustum correction, offscreen reflection coordinates, texture recovery, 1x/2x preference, package identity and signing configuration from 0.6.18 remain in place. The renderer still uses the established CPU transforms with GLES rasterization; this revision does not enable the experimental hardware-transform path.

Host regressions and benchmarks are not phone FPS measurements. The next device run is required to quantify the remaining hitches and improvement in sustained gameplay.
