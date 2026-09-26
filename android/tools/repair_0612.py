#!/usr/bin/env python3
"""Apply the checksum-verified 0.6.12 patch to the known 0.6.11 baseline.

The transport parts are expanded into a readable unified diff and removed.
CI commits the actual readable source only after all regression tests pass.
No network access, package installation, or Git push occurs in this script.
"""
from pathlib import Path
import base64
import hashlib
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'android/tools'
EXPECTED = '9341c4569ea5e357dad675fe4589fb20168d5473500e121f6ca5904022b80c41'

def main():
    if (ROOT / 'android/REPAIR_0612.md').exists():
        version = (ROOT / 'android/app/build.gradle').read_text()
        if 'versionName "0.6.12"' not in version:
            raise RuntimeError('0.6.12 marker/version mismatch')
        print('0.6.12 source already applied')
        return
    readable = TOOLS / 'repair_0612.patch'
    parts = [TOOLS / f'0612-part{i}.txt' for i in range(4)]
    if readable.exists():
        patch = readable.read_bytes()
    else:
        text = ''.join(p.read_text().strip() for p in parts)
        patch = zlib.decompress(base64.b64decode(text, validate=True))
    actual = hashlib.sha256(patch).hexdigest()
    if actual != EXPECTED:
        raise RuntimeError(f'Patch checksum mismatch: {actual}')
    if len(patch) != 50283:
        raise RuntimeError('Patch size mismatch')
    if 'versionName "0.6.11"' not in (ROOT / 'android/app/build.gradle').read_text():
        raise RuntimeError('This migration requires the unmodified 0.6.11 baseline')
    subprocess.run(['git', 'apply', '--check', '-'], input=patch, cwd=ROOT, check=True)
    subprocess.run(['git', 'apply', '-'], input=patch, cwd=ROOT, check=True)
    readable.write_bytes(patch)
    for part in parts:
        if part.exists():
            part.unlink()
    print('Applied exact 0.6.12 source patch:', actual)

if __name__ == '__main__':
    main()
