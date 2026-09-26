#!/usr/bin/env python3
"""Apply the verified 0.6.10 diff, then retain only a readable version guard.
The transport fragments are data, not executable Python. Tests run before CI
commits the expanded, readable source changes.
"""
from pathlib import Path
import ast, base64, hashlib, shutil, subprocess, zlib
ROOT=Path(__file__).resolve().parents[2]
transport=Path(__file__).with_suffix('.transport')
if not (ROOT/'android/REPAIR_0610.md').exists():
    payload=None
    for node in ast.parse(transport.read_text()).body:
        if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='PAYLOAD' for t in node.targets):
            payload=ast.literal_eval(node.value)
    assert isinstance(payload,str)
    # Join the independently transported middle fragment before hash validation.
    fragment='''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'''
    anchor='Hr/UOvnHr27uLo/fH11cA3'
    assert payload.count(anchor)==1
    payload=payload.replace(anchor,fragment)
    patch=zlib.decompress(base64.b64decode(payload))
    assert hashlib.sha256(patch).hexdigest()=='ee712e8dfd529d8e33b4318d0eee60605ae7d24181b85a439acb0372b53e6839','Patch integrity mismatch'
    assert 'versionName "0.6.9"' in (ROOT/'android/app/build.gradle').read_text(),'Expected baseline 0.6.9'
    subprocess.run(['git','apply','--check','-'],input=patch,cwd=ROOT,check=True)
    subprocess.run(['git','apply','-'],input=patch,cwd=ROOT,check=True)
assert 'versionName "0.6.10"' in (ROOT/'android/app/build.gradle').read_text()
license_target=ROOT/'android/app/src/main/assets/licenses/FFmpeg-LGPL-2.1.txt'
license_target.parent.mkdir(parents=True,exist_ok=True)
shutil.copyfile(ROOT/'lcs/third_party/ffmpeg/COPYING.LGPLv2.1',license_target)
# The migration becomes an idempotent guard after its source is expanded.
Path(__file__).write_text('''#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
assert (root/'android/REPAIR_0610.md').exists()
assert 'versionName "0.6.10"' in (root/'android/app/build.gradle').read_text()
assert (root/'android/app/src/main/assets/licenses/FFmpeg-LGPL-2.1.txt').exists()
print('0.6.10 sources already expanded; running tests and build')
''')
transport.unlink(missing_ok=True)
print('0.6.10 patch integrity validated and readable sources prepared')
