#!/usr/bin/env python3
"""Check native identity, ARM64 ELF and packaged media dependencies, not gameplay."""
from pathlib import Path
import hashlib, subprocess, sys, zipfile
apk=Path(sys.argv[1])
sha=subprocess.check_output(['git','rev-parse','--short=12','HEAD'],text=True).strip()
expected=('LCS Android 0.6.13 source='+sha).encode()
with zipfile.ZipFile(apk) as z:
    name='lib/arm64-v8a/liblcsrecomp.so'; data=z.read(name)
    assert data[:5]==b'\x7fELF\x02' and int.from_bytes(data[18:20],'little')==183
    assert expected in data, 'APK native build ID mismatch'
    libs=[n for n in z.namelist() if n.startswith('lib/') and n.endswith('.so')]
    for component in ['avcodec','avformat','avutil','swresample','swscale']:
        assert any(n.startswith('lib/arm64-v8a/lib'+component) for n in libs), component
    assert all(n.startswith('lib/arm64-v8a/') for n in libs), libs
lines=[expected.decode(),'sha256='+hashlib.sha256(apk.read_bytes()).hexdigest(),
       'PASS: native identity matches compiled source; ARM64 ELF; FFmpeg shared libraries packaged']
print('\n'.join(lines))
apk.with_suffix('.apk.sha256').write_text(lines[1].split('=',1)[1]+'  '+apk.name+'\n')
apk.with_name('build-identity.txt').write_text('\n'.join(lines)+'\n')
