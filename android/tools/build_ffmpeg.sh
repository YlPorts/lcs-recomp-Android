#!/usr/bin/env bash
set -euo pipefail
# FFmpeg 7.1.5, exact commit resolved from its signed release tag.
readonly FFMPEG_COMMIT=3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:-android}"
SOURCE="$ROOT/android/.deps/ffmpeg-source"
PREFIX="$ROOT/android/.deps/ffmpeg-$MODE"
BUILD="$ROOT/android/.deps/ffmpeg-build-$MODE"
mkdir -p "$SOURCE" "$PREFIX" "$BUILD"
if [[ ! -f "$SOURCE/configure" ]]; then
  git -C "$SOURCE" init -q
  git -C "$SOURCE" remote add origin https://github.com/FFmpeg/FFmpeg.git
  git -C "$SOURCE" fetch --depth=1 origin "$FFMPEG_COMMIT"
  git -C "$SOURCE" checkout --detach FETCH_HEAD
fi
[[ "$(git -C "$SOURCE" rev-parse HEAD)" == "$FFMPEG_COMMIT" ]]
COMMON=(--prefix="$PREFIX" --disable-programs --disable-doc --disable-network
  --disable-autodetect --disable-everything --enable-shared --disable-static
  --enable-avcodec --enable-avformat --enable-avutil --enable-swresample --enable-swscale
  --disable-avdevice --disable-avfilter --disable-postproc --enable-pthreads --enable-pic
  --enable-decoder=atrac3,atrac3p,pcm_s16le,mp3,mp3float,h264
  --enable-demuxer=wav,oma,mp3,mpegps --enable-parser=h264,mpegaudio --enable-protocol=file)
if [[ "$MODE" == android ]]; then
  NDK="${ANDROID_NDK_HOME:-${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}/ndk/27.2.12479018}"
  TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
  [[ -x "$TOOLCHAIN/bin/aarch64-linux-android26-clang" ]]
  COMMON+=(--target-os=android --arch=aarch64 --enable-cross-compile
    --cc="$TOOLCHAIN/bin/aarch64-linux-android26-clang"
    --cxx="$TOOLCHAIN/bin/aarch64-linux-android26-clang++"
    --ar="$TOOLCHAIN/bin/llvm-ar" --ranlib="$TOOLCHAIN/bin/llvm-ranlib"
    --strip="$TOOLCHAIN/bin/llvm-strip" --nm="$TOOLCHAIN/bin/llvm-nm"
    --extra-ldflags="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")
elif [[ "$MODE" == host ]]; then
  COMMON+=(--disable-x86asm)
else
  echo "usage: $0 [host|android]" >&2;exit 2
fi
# Fingerprint includes the build script and pin; never silently reuse old configuration.
KEY="$(sha256sum "$0" | cut -d' ' -f1)-$FFMPEG_COMMIT"
if [[ -f "$PREFIX/build-id.txt" ]] && [[ "$(cat "$PREFIX/build-id.txt")" == "$KEY" ]]; then
  echo "FFmpeg $MODE cached: $FFMPEG_COMMIT";exit 0
fi
cd "$BUILD"
"$SOURCE/configure" "${COMMON[@]}"
make -j "${FFMPEG_JOBS:-2}"
make install
mkdir -p "$PREFIX/licenses"
cp "$SOURCE/COPYING.LGPLv2.1" "$PREFIX/licenses/"
printf '%s\n' "$KEY" > "$PREFIX/build-id.txt"
printf '%s\n' "$FFMPEG_COMMIT" > "$PREFIX/source-commit.txt"
if [[ "$MODE" == android ]]; then
  # Android's dynamic linker needs unversioned library names.
  for lib in avcodec avformat avutil swresample swscale; do
    [[ -f "$PREFIX/lib/lib$lib.so" ]]
    "$TOOLCHAIN/bin/llvm-readelf" -d "$PREFIX/lib/lib$lib.so" | grep SONAME
  done
fi
