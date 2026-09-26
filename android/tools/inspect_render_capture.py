#!/usr/bin/env python3
"""Inspect a local LCS render capture; no uploads, edits to game data, or dependencies."""
import argparse,html,json,re,struct,zlib,zipfile
from pathlib import Path

def png(rgba,w,h,bottom_up=False):
 def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
 rows=range(h-1,-1,-1) if bottom_up else range(h)
 raw=b''.join(b'\0'+rgba[y*w*4:(y+1)*w*4] for y in rows)
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('capture',type=Path);ap.add_argument('--out',type=Path,default=Path('capture-review'));args=ap.parse_args()
 args.out.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(args.capture) as z:
  info=z.infolist();assert len(info)<=17000 and sum(x.file_size for x in info)<=70*1024*1024,'Capture exceeds diagnostic bounds'
  manifest=json.loads(z.read('manifest.json'));assert manifest['format']=='LCS-GE-capture-1'
  print(json.dumps(manifest,indent=2));items=[]
  for name in z.namelist():
   if name!='frame.rgba' and not re.fullmatch(r'gpu/textures/\d+_\d+\.rgba',name):continue
   metadata=name[:-5]+'.json';m=json.loads(z.read(metadata));w=int(m['width']);h=int(m['height'])
   assert 0<w<=4096 and 0<h<=4096
   data=z.read(name);assert len(data)==w*h*4
   output='frame.png' if name=='frame.rgba' else Path(name).stem+'.png'
   (args.out/output).write_bytes(png(data,w,h,m.get('bottomUp',False)))
   # Ranking only: green grass/paint can be legitimate. No recolouring is applied.
   green=sum(g>80 and g>r*1.5 and g>b*1.5 for r,g,b,a in zip(data[0::4],data[1::4],data[2::4],data[3::4]))
   items.append((name=='frame.rgba',green/(w*h),output,w,h))
  items.sort(reverse=True)
  page=['<!doctype html><meta charset="utf-8"><title>LCS capture review</title><h1>Local render capture</h1><p>Green fraction ranks candidates only; it does not establish an error. Images are unchanged. GPU texture rows are shown in increasing V order.</p>',
        '<pre>'+html.escape(json.dumps(manifest,indent=2))+'</pre>']
  for frame,fraction,output,w,h in items:
   page.append('<figure style="display:inline-block"><img style="max-width:720px;max-height:320px;image-rendering:pixelated" src="'+html.escape(output,quote=True)+'"><figcaption>'+html.escape(output)+f' — {w}×{h}, green fraction {fraction:.1%}</figcaption></figure>')
  (args.out/'index.html').write_text('\n'.join(page),encoding='utf-8')
  print('Review:',args.out/'index.html','; decoded images:',len(items))
if __name__=='__main__':main()
