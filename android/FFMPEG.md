# Android FFmpeg integration

FFmpeg 7.1.5 is pinned to commit 3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587
from the upstream signed n7.1.5 tag.

Build using `bash android/tools/build_ffmpeg.sh android` before Gradle.
No GPL, nonfree or external codec components are enabled. The five libraries
are shared, replaceable LGPL-2.1-or-later libraries. A full corresponding source
archive and the build configuration are published with the CI artifacts.
The installed license files are in the artifact and Android assets.
No PSP game data or codec test media are included in the APK or source archive.

This activates the existing AT3/OMA AudioStreamDecoder instead of the stub.
The separate PMF movie presentation path is still skipped. The log lists the
actual opened codec and reports failures, so missing voice files can be
distinguished from audio-production starvation.
