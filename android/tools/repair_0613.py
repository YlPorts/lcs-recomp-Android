#!/usr/bin/env python3
"""Apply the reviewed 0.6.13 patch atomically after validating its exact bytes.

The small transport parts are base64-encoded zlib, not executable code. CI
commits the resulting readable source and plain unified diff after tests pass.
No external programs are fetched, no Git refs are changed by this script.
"""
from pathlib import Path
import base64
import hashlib
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
PART_HASHES = (
    'eab64e860ddd4d3f1df30419843e81e2fb0ca9a0',
    '955ee1313d80f6ea2c84fc7ee3c9d1d45a14e006',
    '505b8a50c6f4a2ae79603b97e03d1f23146330d9',
    '54df1045c032119f92d7b043689830e441818167',
)
PATCH_SHA256 = '1aaa0aa04b62e24302ee6c7a2e8975c12262332ce49882754aca40b6e2c9c708'


def apply() -> None:
    if (ROOT / 'android/REPAIR_0613.md').is_file():
        print('0.6.13 source migration already applied; no changes')
        return
    parts = []
    for number, expected in enumerate(PART_HASHES, 1):
        path = ROOT / f'android/tools/repair_0613.patch.b64.part{number}'
        data = path.read_bytes()
        digest = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        if digest != expected:
            raise RuntimeError(f'Patch transport checksum mismatch: {path.name}')
        parts.append(data)
    payload = zlib.decompress(base64.b64decode(b''.join(parts), validate=True))
    if len(payload) != 65010 or hashlib.sha256(payload).hexdigest() != PATCH_SHA256:
        raise RuntimeError('Unified diff checksum mismatch; no sources changed')
    if 'versionName "0.6.12"' not in (ROOT / 'android/app/build.gradle').read_text():
        raise RuntimeError('Expected unmodified 0.6.12 baseline')
    patch = ROOT / 'android/tools/repair_0613.patch'
    patch.write_bytes(payload)
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=ROOT, check=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=ROOT, check=True)
    if 'versionName "0.6.13"' not in (ROOT / 'android/app/build.gradle').read_text():
        raise RuntimeError('Source update did not set the expected native version')
    print(f'Applied reviewed Android 0.6.13 source patch: sha256={PATCH_SHA256}')


if __name__ == '__main__':
    apply()
