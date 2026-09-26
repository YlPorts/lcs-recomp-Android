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
assert 'getInt("render_scale", 2)' in java
assert 'putInt("render_scale", scale)' in java
assert 'jint render_scale)' in bridge
assert 'const bool scale2 = render_scale == 2;' in bridge
assert 'setenv("PSPRECOMP_GLES_SCALE", scale2 ? "2" : "1", 1);' in bridge
assert 'versionName "0.6.12"' in (root/'android/app/build.gradle').read_text()
assert 'LCS Android 0.6.12' in (root/'lcs/host/lcs_android_build.cpp').read_text()
print('PASS: persisted 1x/2x launcher -> JNI -> internal scale config; default 2x; native version 0.6.12')
