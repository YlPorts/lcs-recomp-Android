# Android 0.6.8

## Changes
- Resolve skipped texture signatures to the last validated content and pin them when queuing a draw.
- Restrict framebuffer feedback lookup to PSP VRAM, including uncached aliases; do not alias main RAM.
- Preserve ordering for feedback primitives and flip only GLES-rendered feedback.
- Clear-depth draws write depth with GL_ALWAYS even when PSP Z test is disabled.
- Implement PSP colour tests in GLES; do not remove green with a colour heuristic.
- Reuse indexed-vertex transforms within each primitive; AOT uses O2 without fast-math.
- Replace the mutex-dependent AAudio consumer with immutable timed PCM packets.
- Log successful presentations per wall-clock second separately from CPU submit cost.
- Disable the duplicate LCS_FPS_CAP per-GE-list throttle. Keep the existing vblank real-time pacing and game frame-limiter configuration.

## Verification
Component regression tests passed, including 10,000 concurrent audio packet transfers. The same tests passed with AddressSanitizer and UndefinedBehaviorSanitizer. All four actual GLES 3 shaders passed glslangValidator.

The actual shaders also compiled, linked and rendered a synthetic textured quad through Mesa llvmpipe (not an Android/Mali device). RGB tests (disabled/equal/not-equal/never/always) and one-level mip completeness passed. Reproduce with `python3 android/tests/gles_smoke.py` on Linux with surfaceless Mesa EGL.

An isolated test of the actual cap_frame_rate function measured 24 changing GE-list addresses: about 0.383 seconds with the old 60-list/sec cap versus microseconds with the redundant cap disabled. Reproduce with `python3 android/tests/pacing_regression.py`. This is NOT a game FPS benchmark.

These tests do not certify 60 FPS, continuous audio under sustained slow emulation, or game-wide visual accuracy. Physical-device gameplay validation remains necessary. `presentFPS` counts successful window presentations, which may still include intermediate GE list finishes; it is not proof of 60 unique complete game frames.
