#!/usr/bin/env python3
"""Apply the reviewed/tested 0.6.11 diff to a matching 0.6.10 checkout.
The workflow commits the resulting readable source after regressions pass.
"""
from pathlib import Path
import gzip, hashlib, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
marker=root/'android/REPAIR_0611.md'
if marker.is_file():
    assert 'versionName "0.6.11"' in (root/'android/app/build.gradle').read_text()
    print('0.6.11 already applied; no source changes')
else:
    compressed=b''.join((root/f'android/patches/0.6.11.patch.gz.{i}').read_bytes() for i in range(3))
    patch=gzip.decompress(compressed)
    assert hashlib.sha256(patch).hexdigest() == '29ec023a154b031ba3a5f62d68ca562ede574dc75efbdc7886b88ae3634c00f9', 'Patch integrity failure'
    with tempfile.NamedTemporaryFile(suffix='.patch') as f:
        f.write(patch);f.flush()
        subprocess.run(['git','apply','--check',f.name],cwd=root,check=True)
        subprocess.run(['git','apply',f.name],cwd=root,check=True)
    print('Applied 0.6.11 blend, lighting and build identity diff')
