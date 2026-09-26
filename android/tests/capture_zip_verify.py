from pathlib import Path
import json,sys,zipfile
root=Path(sys.argv[1])
for name in ('normal','limited','marked'):
 with zipfile.ZipFile(root/(name+'.zip')) as z:
  assert z.testzip() is None
  m=json.loads(z.read('manifest.json'))
  assert m['truncated']==(name!='normal')
  if name=='normal':assert z.read('binary.bin')==bytes(range(256)) and z.read('empty.bin')==b'' and m['draws']==2
  assert len(set(z.namelist()))==len(z.namelist())
if (root/'gpu.zip').exists():
 with zipfile.ZipFile(root/'gpu.zip') as z:
  assert z.testzip() is None
  images=[n for n in z.namelist() if n.startswith('gpu/textures/') and n.endswith('.rgba')]
  assert len(images)==1,images
  assert z.read(images[0])==bytes([255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255])
  assert len(z.read('gpu/batch_0.vertices'))==6*36
  meta=json.loads(z.read('gpu/batch_0.json'))
  assert meta['textureAddress']==0x088a0000 and meta['vertexStride']==36
  assert not any(n.endswith('.descriptor') for n in z.namelist())
  assert json.loads(z.read('manifest.json'))['truncated'] is False
 print('PASS: actual GPU cache bytes verified independently, explicit descriptor metadata, no raw padded native structs')
print('PASS: independent ZIP CRC, exact contents, unique names and truncation verification')
