#!/usr/bin/env python3
"""Read back actual ES3 fragment-shader results. No scene/FPS claim."""
from pathlib import Path
import sys
smoke=Path(__file__).with_name('gles_smoke.py')
text=smoke.read_text()
end="api('eglMakeCurrent',U,V,V,V,V)(d,None,None,None)"
prefix=text[:text.index(end)]
if len(sys.argv)>1:
    # Validate an earlier real shader against exactly the same test.
    prefix=prefix.replace("code=(Path(__file__).resolve().parents[2]/'lcs/host/ge_gpu_backend_gles.cpp').read_text()",
                          "code=Path("+repr(str(Path(sys.argv[1]).resolve()))+").read_text()")
ns={'__file__':str(smoke)}
exec(compile(prefix,str(smoke),'exec'),ns)
gl=ns['api'];C=ns['C'];I=ns['I'];U=ns['U'];V=ns['V'];F=ns['F']
seti=ns['seti'];u2=ns['u2'];loc=ns['loc'];p=ns['p'];params=ns['params']
seti('uColorTest',-1);seti('uTextureEnabled',1);seti('uTextureFunction',3)
seti('uFogEnabled',0);seti('uAlphaEnabled',0)
# An asymmetric four-colour image makes a mistaken crop/vertical scale obvious.
pixels=(C.c_ubyte*16)(255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255)
gl('glTexImage2D',None,U,I,I,I,I,I,U,U,V)(0x0DE1,0,0x8058,2,2,0,0x1908,0x1401,C.cast(pixels,V))
params(0x0DE1,0x2801,0x2600);params(0x0DE1,0x2800,0x2600)
params(0x0DE1,0x2802,0x812F);params(0x0DE1,0x2803,0x812F)
def render(umax,vmax,flip,sx,sy):
    values=[]
    for x,y,u,v in [(0,0,0,0),(16,0,umax,0),(0,16,0,vmax),(0,16,0,vmax),(16,0,umax,0),(16,16,umax,vmax)]:
        values.extend([x,y,0,1,1,1,1,1,u,v,1,1])
    data=(F*len(values))(*values)
    gl('glBindBuffer',None,U,U)(0x8892,ns['vbo'])
    gl('glBufferData',None,U,C.c_ssize_t,V,U)(0x8892,C.sizeof(data),C.cast(data,V),0x88E4)
    seti('uTextureFlipV',flip);u2(loc(p,b'uFeedbackScale'),sx,sy)
    ns['clearcolor'](0,0,0,1);ns['clear'](0x4000);ns['draw'](4,0,6)
    out=(C.c_ubyte*(16*16*4))();ns['read'](0,0,16,16,0x1908,0x1401,C.cast(out,V))
    return bytes(out)
try:
    for sx,sy in [(1,1),(2,2),(1,512/320),(2,1),(0.5,1),(1,0.5),(0.5,2),(4,4)]:
        u,v=min(1,sx),min(1,sy)
        reference=render(u,v,1,1,1)
        actual=render(u/sx,v/sy,1,sx,sy)
        assert actual==reference,f'Feedback extent mismatch: {sx}, {sy}'
    reference=render(1,1,0,1,1)
    assert render(1,1,0,9,7)==reference,'Ordinary decoded textures changed'
    assert gl('glGetError',U)()==0
    print('PASS: 8 real shader feedback-extent readbacks; regular decoded texture sampling unchanged')
finally:
    gl('eglMakeCurrent',U,V,V,V,V)(ns['d'],None,None,None)
    gl('eglDestroyContext',U,V,V)(ns['d'],ns['ctx'])
    gl('eglDestroySurface',U,V,V)(ns['d'],ns['surf'])
    gl('eglTerminate',U,V)(ns['d'])
