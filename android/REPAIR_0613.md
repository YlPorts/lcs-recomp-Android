# Android 0.6.13

Baseline: e5e48f760c7ec3b73a1d7a1055b9d7a3f9128a0a (0.6.12).

## Texture correctness
- T4 textures with separate mip palettes use the per-level 16-entry bank after start/shift/mask. Shared palettes and T8/T16/T32 remain unchanged. Tests invoke the actual texture reader, not a replacement decoder.
- Respect constant PSP texture LOD and the LOD offset in the GLES shader. Adjacent draws using different LOD settings cannot merge. Automatic mode retains host derivative-based LOD plus the guest bias; slope mode is not newly implemented.
- Hash all source texels when validating an image. The previous sampling hash for images larger than 4096 bytes could miss atlas/dynamic updates in unchecked regions.
- The GLES decoded-image identity uses the effective loaded palette lookups, not unused palette bytes or the old RAM palette address. Sampler, filtering and LOD settings remain per-draw and no longer duplicate identical RGBA images.

## CPU and memory
- Separate draw, vertex and lighting revisions in the actual GE command interpreter. Reuse exact decoded vertex records across adjacent primitives only while all relevant state stays identical. New lists, matrices, materials, lighting, UV/fog and source-byte changes invalidate reuse. Byte comparisons prevent record-hash collisions from reusing incorrect vertices.
- Repeated identical CLUT loads return before allocating/copying another snapshot. Point/spot lighting still bypasses directional-light memoization.
- Upload only the GLES-consumed full-precision vertex attributes: 36 bytes instead of the desktop 56-byte record.
- Use three persistent geometry buffer pairs, guarded by GL fences before reuse, instead of orphaning storage on every frame. This bounds geometry allocations; it does not bound total process RSS or all driver memory. A failed wait refuses to overwrite a busy buffer.
- Set a 96 MiB decoded-image budget and keep 8192 entry capacity. Evict old entries in one sorted pass, never images referenced by the current queued frame. A frame larger than the budget may temporarily exceed it. Prune obsolete texture-version metadata periodically.
- Preserve existing 1x/2x selections. Fresh installations default to 1x; the launcher still offers 2x. No hidden resolution switching or frame skipping.
- Preserve ATRAC decoders, continuous audio queue, one GLES owner thread, guest speed and vblank timing. No new PMF support or audio time stretching.

## Verification and limits
- The actual 0.6.12 texture reader fails the new separate-mip-palette and unsampled-atlas-update tests; this revision passes.
- Effective-palette invalidation, actual GE revision propagation, 128000 exact cross-draw vertex comparisons, mutated source records and material/UV/world changes are tested. Existing vertex, geometry, lighting and audio regressions remain.
- The full GLES backend runs on Mesa with coherent framebuffer fetch and ordered-copy fallback: existing 2178 blend cases, clipping, target scales and texture versions, plus fixed LOD red/blue mip images, sampler sharing, 100 fenced buffer reuse/readback iterations and pending-frame eviction protection.
- Sanitizers run on the new CPU state/texture tests and existing audio/geometry tests. Native APK identity and media dependencies are checked after compilation.

See CI artifacts for executed outputs. Local Mesa tests used standard GL/EGL ABI declarations; CI compiles against official Mesa headers and Android uses NDK headers. These tests are not Samsung A15 gameplay benchmarks. 60 sustained FPS, a particular RSS reduction, elimination of all flicker, or continuous audio during sustained slow guest execution are NOT certified.
