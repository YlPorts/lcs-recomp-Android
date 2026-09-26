#!/usr/bin/env python3
"""Wiring contract test; actual FBO sizes are tested separately in GLES."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
java=(root/'android/app/src/main/java/com/ylports/lcsrecomp/MainActivity.java').read_text()
bridge=(root/'android/app/src/main/cpp/android_bridge.cpp').read_text()
assert 'native int nativeRun(String gameRoot, String configPath, int renderScale)' in java
assert 'nativeRun(gameRoot, configPath, renderScale())' in java
assert '"InternalScale=" + renderScale()' in java
assert 'getInt("render_scale", 1)' in java
assert 'putInt("render_scale", scale)' in java
assert 'jint render_scale)' in bridge
assert 'const bool scale2 = render_scale == 2;' in bridge
assert 'setenv("PSPRECOMP_GLES_SCALE", scale2 ? "2" : "1", 1);' in bridge
gradle=(root/'android/app/build.gradle').read_text()
match=re.search(r'versionName\s+"([0-9]+\.[0-9]+\.[0-9]+)"',gradle)
assert match is not None, 'Missing semantic Gradle version'
version=match.group(1)
native=(root/'lcs/host/lcs_android_build.cpp').read_text()
assert f'LCS Android {version} source=' in native, 'Native and Gradle versions differ'
assert 'LCS_ANDROID_SOURCE_SHA' in native, 'Missing native source identity'
print(f'PASS: persisted 1x/2x launcher -> JNI -> internal scale config; default 1x; existing preference retained; matching native version {version}')
