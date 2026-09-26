#!/usr/bin/env python3
"""Apply the reviewed 0.6.14 migration. All writes are prevalidated."""
from pathlib import Path
import base64,hashlib,subprocess,zlib
ROOT=Path(__file__).resolve().parents[2]
BEFORE={'android/REPAIR_0614.md': None, 'android/app/build.gradle': 'a5c96a16122b6b0fdbd17f12787029274ae6f2e27a2da5e25134289c8b225772', 'android/tests/audio_deadline_regression.cpp': None, 'android/tests/ge_control_regression.cpp': None, 'android/tests/gles_depth_regression.cpp': None, 'android/tests/verify_apk.py': 'b9e748d75be92ccf66252a390649c99dc8a0a221b16e762aee99cffc174ee4da', 'lcs/host/ge_gpu_backend.hpp': 'a9b905c524db5359a9873461d09d7fbf5c536b507404ea628300c64559ba1eae', 'lcs/host/ge_gpu_backend_gles.cpp': '519563f0950308d9671e2c4290348e7e56f9d48316d62d77e9e14358762f3ead', 'lcs/host/ge_renderer.cpp': '093414609d0a431b8a9c0cb99a664e1335525901ae0e922b5322096e4d60637d', 'lcs/host/ge_renderer.hpp': 'bb362bda6920039a5dab7fae5e593f65dd0d22322da2576bb87539edc0632a50', 'lcs/host/lcs_android_build.cpp': '9deb183d0a5b6b7bb3b7a20eedc00a2d997bf0fb8c1d462d90a947f219047aeb', 'lcs/host/lcs_audio_output_android.inc': '41687029e2ad4f645c75f13f273c2d27a7a2e3dd439020273443d7d20d40d88f', 'lcs/host/lcs_ge_exec.cpp': 'b4855b8da57740ec9e1528e919fb2c64c0e3647a94a0abf9928a3594fd534614', 'lcs/host/lcs_profile.cpp': 'f75a762f72b772fa771585c836fd37912e9f384691f4801f05ed11c7a0794595'}
PATCH_SHA='54697dc71c17bd21bcd8d5c6061be9d6e5a64cf076cf24cd6ef4574f9dbab858'
def main():
    if (ROOT/'android/REPAIR_0614.md').exists():
        print('0.6.14 source migration already applied');return
    for name,expected in BEFORE.items():
        path=ROOT/name
        if expected is None:
            if path.exists():raise RuntimeError('Unexpected existing new file: '+name)
        elif not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=expected:
            raise RuntimeError('Baseline mismatch; refusing to alter '+name)
    encoded=''.join((ROOT/f'android/tools/repair_0614.b64.part{i}').read_text().strip() for i in range(1,5))
    patch=zlib.decompress(base64.b64decode(encoded))
    if hashlib.sha256(patch).hexdigest()!=PATCH_SHA:raise RuntimeError('Corrupt patch payload')
    subprocess.run(['git','apply','--check','-'],input=patch,cwd=ROOT,check=True)
    subprocess.run(['git','apply','-'],input=patch,cwd=ROOT,check=True)
    print('Applied checked 0.6.14 changes to',len(BEFORE),'source/test files')
if __name__=='__main__':main()
