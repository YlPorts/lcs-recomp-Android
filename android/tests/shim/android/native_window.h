#pragma once
// Test-only Android window stand-in; the GLES test uses a Mesa pbuffer.
struct ANativeWindow {};
inline void ANativeWindow_acquire(ANativeWindow *) {}
inline void ANativeWindow_release(ANativeWindow *) {}
