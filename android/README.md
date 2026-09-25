# LCS Recomp Android

Experimental Android/ARM64 port of LCS Recomp.

## Current v0.1 bootstrap

- Android 8.0+ (API 26+), ARM64 only.
- Native PSP recompilation core is built with the Android NDK.
- Existing software GE renderer is preserved for the first mobile bring-up.
- Frames are presented through OpenGL ES 3.
- Multitouch overlay includes analog movement, camera stick, PSP face buttons,
  D-pad, L/R, Start and Select.
- The launcher imports the user's own game files through Android's Storage
  Access Framework. No proprietary game data is included in the APK.

## Required game files

Use the US v1.05 release (ULUS-10041), matching the upstream recomp project.
Select a folder containing:

```
EBOOT.ELF
PSP_GAME/
```

The EBOOT must be decrypted, as required by the upstream project.

## Current limitations

This first Android stage is intentionally focused on getting the native
recompiled game running on ARM64:

- Audio output is disabled.
- FFmpeg/PMF media decoding is stubbed on Android.
- The DirectX 12 GE backend remains Windows-only; Android currently uses the
  portable software path plus OpenGL ES presentation.
- Vulkan GPU rendering is the next performance milestone.

## Build

Install JDK 17, Android SDK 35, NDK 27.2.12479018 and CMake 3.22.1, then run:

```sh
gradle -p android :app:assembleDebug
```

GitHub Actions also builds and uploads the debug APK from the `android-port`
branch.
