# Android 0.6.9

Baseline: d7c06ab7 (0.6.8).

- Preserve queued audio across guest timestamp jitter. Explicit channel reset still cancels that channel. Real underruns use a short cursor rebase, not another full silent lead.
- Log mixed samples, invalidated samples and starvation rebases separately from device xruns.
- Present complete ordered GE batches at guest vblank, rather than intermediate command-list changes. Existing guest timing remains enabled; GLES still has one owner thread.
- Revalidate texture contents on new lists, TEXFLUSH, TEXSYNC and CLUTLOAD without deleting versions pinned by queued draws.
- Prepare CPU lights once per primitive, reuse state revisions, trivially accept/reject triangles before polygon clipping, and skip disabled diagnostic bounds.
- Cache GLES pixel/transform uniforms and ES3 samplers. Keep draw order and framebuffer feedback handling.
- Log CPU GE, texture, vertex, clipping, submission, wait and remaining wall time separately.

## Verification
Tests exercise the actual producer/callback (AAudio driver mocked), CPU lighting/UV and clipping helpers, queues, real shaders and full GLES backend on Mesa. See CI artifacts for the executed results.

The audio regression supplies 46080 samples with guest-time jitter. 0.6.8 cancels almost all pending PCM; this revision must reproduce all supplied samples with zero jitter-induced resets. This does not synthesize missing audio when emulation is persistently slow.

No physical-device test is available yet for this revision. 60 FPS, continuous audio in every scene, and elimination of every visual defect are not certified. CPU submit timings are not GPU timer-query measurements.
