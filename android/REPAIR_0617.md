# Android 0.6.17

Base: `4bfcfee69084755a08150dc56ebd7fd210e230c4` (0.6.16). Version code: 23.

## Evidence from the supplied device capture

The supplied capture identifies `LCS Android 0.6.16 source=4bfcfee69084`. Its CPU inputs contain 2,125 draws and 95,508 submitted vertices. Of these, 68,450 vertices (71.7%) use layout `0x115`: an unskinned 10-byte record with 16-bit vertex colour, 8-bit texture coordinates and 16-bit positions. All captured draws of this layout enable global lighting but disable all four individual lights; they use ordinary UV mapping and fog. Each such draw contains only 1–16 distinct vertex colours.

The supplied 1x device log reports about 20–22 presentations/second and 45–50 ms per frame, including approximately 13–15 ms in vertex processing. These are the user's 0.6.16 measurements, not measurements of 0.6.17. The capture and runtime log do not establish that every reported material error has one cause.

The ZIP contains 16,001 entries and marks itself truncated. It retains CPU draw inputs but GPU records only through batch 358; later vehicle framebuffer readbacks are missing from the diagnostic, which does not mean those textures were missing in the game. Fifty environment-map draws sample a 64×64 RGB565 offscreen image generated from a scene framebuffer. Replaying the actual decoder for their 3,249 vertices produced zero green-dominant vertex colours: every result satisfies R ≥ G ≥ B. This directs investigation toward framebuffer generation/sampling while leaving final GPU pixels and unrelated materials unverified.

## Changes

- Add a guarded CPU decoder for ambient-only `0x115` draws. Reuse the existing position, projection, fog and UV calculations. When every individual light is disabled, normal calculations cannot affect the ambient/emissive colour; an exact per-colour cache computes misses using the existing lighting implementation. Full colour keys prevent cache collisions from changing output, and lighting/material revision changes invalidate the cache. Other layouts, individual lights, generated UV modes and software rendering keep the existing decoder.
- Preserve the existing full-record vertex memo, environment-map preparation, early position rejection and guest timing. This change does not enable the experimental full GPU transform path or change game speed.
- Correct GLES render-target and framebuffer-feedback handling derived from the captured small offscreen target. Use the GE region to size the 64×64 offscreen image and a view of the declared texture extent for feedback sampling, so repeat/clamp filtering does not sample unrelated rows from the larger allocation. Invalidate cached views when their source changes and preserve an ordered snapshot for self-feedback. Base sampling on the actual GL allocation, including when a larger target is queued for later execution. Keep the sampling-view cache within a separate 64 MiB retention budget.
- Raise the optional render-capture entry budget from 16,000 to 32,000 so dense frames can retain more GPU-stage evidence. The 64 MiB payload bound remains unchanged. Capture remains manually requested and may briefly pause rendering.
- Record each GPU batch's feedback source stride, format, actual allocation and crop/match state at execution time. Later queued target growth no longer rewrites the diagnostic's account of an earlier batch.

## Verification and limits

A separate replay executable compiled against the unmodified 0.6.16 renderer produced the reference outputs. The 0.6.17 executable matched all 95,508 captured vertices bit for bit, including colour, position, W, inverse W, UV/Q and fog. Both runs produced 32,841 full-record memo hits and 197 indexed-vertex hits. The new path processed 43,124 vertices remaining after reuse.

Seven trials of ten captured CPU vertex frames each measured a host median of **6.548 ms before and 4.747 ms after**. This is replay of the supplied scene's CPU vertex stage, not a physical Android measurement, complete game-frame benchmark or FPS claim. Texture upload, clipping, GPU submission, presentation, audio and guest execution are outside this timing. The capture does not record original command-list boundaries or redundant matrix writes, so replay cache lifetimes follow observed state snapshots. It retains the production memo algorithm, eligibility checks and full-record comparisons; its exact hit count need not equal an uninterrupted device run.

The self-contained `ambient_vertex_regression.cpp` checks 2,048 bit-exact cases across material changes, colour cache reuse, transforms, UVs and fog. It also checks exclusions for individual lights, unsupported layouts/modes, software rendering and truncated input. Optimized and AddressSanitizer/UndefinedBehaviorSanitizer runs passed. Local LeakSanitizer process inspection is unavailable in the execution container; local sanitizer runs disabled leak detection only.

The capture archive tests passed explicit-request/frame-boundary handling, concurrent request rejection, binary and empty entries, the unchanged 64 MiB payload bound, the new 32,000-entry bound, a complete 22,000-entry dense capture, ZIP CRC/content verification and truncation markers. The scale-preference and capture-UI contract checks also passed for version 0.6.17.

`gles_vehicle_feedback_regression.cpp` reproduced stale green on the unmodified baseline: RGB (0,255,0) where the reference 64×64 map is red, exiting with status 20. The corrected capture-derived Mesa fixture passed with both framebuffer fetch and the ordered-copy fallback. Fifty distinct GPU consumers reuse one crop copy. Twenty-four edge comparisons (six UV samples × repeat/clamp × 1x/2x) match a tightly allocated reference texture. The test also checks source-write invalidation, ordered self-feedback, REGION2 bounds, and a queued future 512-wide target while the actual GL allocation is still 64-wide. Creating five approximately 16 MiB sampling views exercises real GL cache eviction: retained views stay within 64 MiB, and destruction returns tracked bytes to zero.

These are targeted GLES results, not replay of the complete game's GPU frame. The combined existing GLES regressions passed with both framebuffer fetch and ordered-copy fallback: the backend suite (including 2,178 blend-image comparisons per mode), shared-depth/recycled-geometry tests, presentation-culling tests, and framebuffer extent/GPU readback tests. Independent GPU capture ZIP verification passed CRC checks, exact 16-byte texture contents, six 36-byte vertex records, metadata and an untruncated status. Actual shader linking and pixel readback smoke tests also passed on host llvmpipe LLVM 20.1.2. These host Mesa results do not measure Android performance.

The existing extent-test fixture now explicitly sets its texture format equal to its RGBA framebuffer format; its previous default selected RGB565 and unintentionally requested a format reinterpretation rather than testing extent mapping.

See `tests/evidence_0617.md` for the public numeric summary. Raw user capture data, extracted vertex fixtures and reference vertex outputs are not repository assets.

## Reproduce the supplied-capture CPU comparison

The ZIP must be supplied separately. Export only the CPU inputs to a temporary binary, compile the unmodified baseline and current replay drivers, then compare their complete outputs:

```sh
python3 android/tools/capture_vertex_fixture.py /path/to/LCS-render-capture.zip /tmp/lcs-vertex-fixture.bin
git show 4bfcfee69084755a08150dc56ebd7fd210e230c4:lcs/host/ge_renderer.cpp > /tmp/lcs-ge-renderer-0616.cpp
g++ -std=c++20 -O3 -ffp-contract=fast -ffunction-sections -fdata-sections -DLCS_BASELINE_REPLAY -DLCS_TEST_RENDERER='"/tmp/lcs-ge-renderer-0616.cpp"' -Iinclude -Ilcs/host android/tests/capture_vertex_replay.cpp src/guest_memory.cpp -Wl,--gc-sections -pthread -o /tmp/lcs-vertex-before
g++ -std=c++20 -O3 -ffp-contract=fast -ffunction-sections -fdata-sections -Iinclude -Ilcs/host android/tests/capture_vertex_replay.cpp src/guest_memory.cpp -Wl,--gc-sections -pthread -o /tmp/lcs-vertex-after
/tmp/lcs-vertex-before /tmp/lcs-vertex-fixture.bin --write-reference /tmp/lcs-vertex-reference.bin
/tmp/lcs-vertex-after /tmp/lcs-vertex-fixture.bin --compare /tmp/lcs-vertex-reference.bin
```

Use the self-contained guard test in CI; running CI does not require or publish this game's captured data.

## Package and device validation

The standard application ID remains `com.ylports.lcsrecomp`. An optional `-PlcsParallelInstall=true` build uses `com.ylports.lcsrecomp.preview` and the launcher label `LCS Recomp 0.6.17 Prueba`; it has separate private preferences/saves and requires selecting the game folder. Updating an installed package still requires a compatible signing key; a version increment alone does not provide signing compatibility. The previous CI's ephemeral debug private key is not present in the repository.

Physical Samsung A15/Mali validation of 0.6.17 is still required. Recheck the same affected cars/objects, movement and sustained frame timing at the same resolution after the APK is installed. The existing touch controls, 1x/2x selection and audio implementation remain unchanged. Do not judge normal rendering speed during the capture pause.
