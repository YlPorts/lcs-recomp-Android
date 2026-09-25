# Android 0.6.8

- Resolve skipped texture signatures to the last validated content and pin them when queuing a draw.
- Restrict framebuffer feedback lookup to PSP VRAM, including uncached aliases.
- Preserve ordering for feedback primitives and flip only GLES-rendered feedback.
- Clear-depth draws write depth with GL_ALWAYS even when PSP Z test is disabled.
- Implement PSP colour tests in GLES; do not remove green with a colour heuristic.
- Reuse indexed-vertex transforms within each primitive; AOT uses O2 without fast-math.
- Replace the mutex-dependent AAudio consumer with immutable timed PCM packets.
- Log actual successful presentations per wall-clock second separately from CPU submit cost.

This does not certify 60 FPS or game-wide visual accuracy on a physical device.
Synthetic tests verify components; a complete game run remains necessary.
