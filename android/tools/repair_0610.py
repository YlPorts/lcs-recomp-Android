#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
assert (root/'android/REPAIR_0610.md').exists()
assert 'versionName "0.6.10"' in (root/'android/app/build.gradle').read_text()
assert (root/'android/app/src/main/assets/licenses/FFmpeg-LGPL-2.1.txt').exists()
print('0.6.10 sources already expanded; running tests and build')
