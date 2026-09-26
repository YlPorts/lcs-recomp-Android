#!/usr/bin/env python3
"""Offscreen ES3 tests against the actual renderer shaders (Mesa, not a phone)."""
import ctypes as C
import re
from pathlib import Path
E=C.CDLL('libEGL.so.1'); I=C.c_int; U=C.c_uint; V=C.c_void_p; F=C.c_float
E.eglGetProcAddress.restype=V; E.eglGetProcAddress.argtypes=[C.c_char_p]
def api(name, result, *params):
    p=E.eglGetProcAddress(name.encode()); assert p,name
    return C.CFUNCTYPE(result,*params)(p)
getdisplay=api('eglGetPlatformDisplayEXT',V,U,V,C.POINTER(I))
d=getdisplay(0x31DD,None,None)
init=api('eglInitialize',U,V,C.POINTER(I),C.POINTER(I)); major=I();minor=I()
assert init(d,C.byref(major),C.byref(minor))
assert api('eglBindAPI',U,U)(0x30A0)
a=(I*13)(0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038)
conf=V();n=I();assert api('eglChooseConfig',U,V,C.POINTER(I),C.POINTER(V),I,C.POINTER(I))(d,a,C.byref(conf),1,C.byref(n)) and n.value
pa=(I*5)(0x3057,16,0x3056,16,0x3038)
surf=api('eglCreatePbufferSurface',V,V,V,C.POINTER(I))(d,conf,pa)
ctx=api('eglCreateContext',V,V,V,V,C.POINTER(I))(d,conf,None,(I*3)(0x3098,3,0x3038))
assert surf and ctx
assert api('eglMakeCurrent',U,V,V,V,V)(d,surf,surf,ctx)
print('GL_RENDERER:', api('glGetString',C.c_char_p,U)(0x1F01).decode())
code=(Path(__file__).resolve().parents[2]/'lcs/host/ge_gpu_backend_gles.cpp').read_text()
shaders=dict(re.findall(r'static constexpr char (\w+)\[\] = R"GLSL\((.*?)\)GLSL";',code,re.S))
create=api('glCreateShader',U,U); source=api('glShaderSource',None,U,I,C.POINTER(C.c_char_p),C.POINTER(I)); compile_=api('glCompileShader',None,U)
shaderiv=api('glGetShaderiv',None,U,U,C.POINTER(I)); shaderlog=api('glGetShaderInfoLog',None,U,I,C.POINTER(I),C.c_char_p)
def shader(text,stage):
    s=create(stage); raw=C.c_char_p(text.encode()); source(s,1,C.byref(raw),None);compile_(s)
    ok=I();shaderiv(s,0x8B81,C.byref(ok))
    if not ok.value:
        log=C.create_string_buffer(8192); shaderlog(s,8192,None,log);raise AssertionError(log.value)
    return s
createprog=api('glCreateProgram',U); attach=api('glAttachShader',None,U,U);link=api('glLinkProgram',None,U);progiv=api('glGetProgramiv',None,U,U,C.POINTER(I))
def program(v,f):
    p=createprog();attach(p,shader(v,0x8B31));attach(p,shader(f,0x8B30));link(p)
    ok=I();progiv(p,0x8B82,C.byref(ok));assert ok.value
    return p
p=program(shaders['kVertexShader'],shaders['kFragmentShader'])
program(shaders['kPresentVertex'],shaders['kPresentFragment'])
api('glUseProgram',None,U)(p)
loc=api('glGetUniformLocation',I,U,C.c_char_p)
u1=api('glUniform1i',None,I,I);u2=api('glUniform2f',None,I,F,F);u3=api('glUniform3i',None,I,I,I,I)
def seti(name,x):u1(loc(p,name.encode()),x)
seti('uMode',2);u2(loc(p,b'uLogicalSize'),16,16);seti('uTextureEnabled',1);seti('uTexture',0);seti('uTextureFunction',3);seti('uTextureUseAlpha',1);seti('uFramebufferFormat',3)
seti('uTextureFlipV',0);u2(loc(p,b'uFeedbackScale'),1,1);seti('uAlphaEnabled',0);seti('uFogEnabled',0);seti('uVertexColorAffine',0)
vao=U(); api('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));api('glBindVertexArray',None,U)(vao)
vbo=U();api('glGenBuffers',None,I,C.POINTER(U))(1,C.byref(vbo));api('glBindBuffer',None,U,U)(0x8892,vbo)
verts=[]
for x,y,u,v in [(0,0,0,0),(16,0,1,0),(0,16,0,1),(0,16,0,1),(16,0,1,0),(16,16,1,1)]:
    verts.extend([x,y,0,1,1,1,1,1,u,v,1,1])
data=(F*len(verts))(*verts)
api('glBufferData',None,U,C.c_ssize_t,V,U)(0x8892,C.sizeof(data),C.cast(data,V),0x88E4)
attrib=api('glVertexAttribPointer',None,U,I,U,U,I,V);enable=api('glEnableVertexAttribArray',None,U)
for index,size,offset in [(0,4,0),(1,4,4),(2,2,8),(3,1,10),(4,1,11)]:
    enable(index);attrib(index,size,0x1406,0,48,V(offset*4))
tex=U();api('glGenTextures',None,I,C.POINTER(U))(1,C.byref(tex));api('glBindTexture',None,U,U)(0x0DE1,tex)
params=api('glTexParameteri',None,U,U,I)
for name,val in [(0x2801,0x2600),(0x2800,0x2600),(0x813C,0),(0x813D,0)]:params(0x0DE1,name,val)
pixels=(C.c_ubyte*4)(0,255,0,255)
api('glTexImage2D',None,U,I,I,I,I,I,U,U,V)(0x0DE1,0,0x8058,1,1,0,0x1908,0x1401,C.cast(pixels,V))
api('glViewport',None,I,I,I,I)(0,0,16,16)
clearcolor=api('glClearColor',None,F,F,F,F);clear=api('glClear',None,U);draw=api('glDrawArrays',None,U,I,I);read=api('glReadPixels',None,I,I,I,I,U,U,V)
u3(loc(p,b'uColorReference'),0,255,0);u3(loc(p,b'uColorMask'),255,255,255)
def render(test):
    seti('uColorTest',test);clearcolor(1,0,0,1);clear(0x4000);draw(4,0,6)
    out=(C.c_ubyte*4)();read(8,8,1,1,0x1908,0x1401,C.cast(out,V));return list(out)
assert render(-1)==[0,255,0,255]
assert render(3)==[255,0,0,255],render(3)
assert render(2)==[0,255,0,255]
assert render(0)==[255,0,0,255]
assert render(1)==[0,255,0,255]
params(0x0DE1,0x2801,0x2703)
assert render(-1)==[0,255,0,255]
err=api('glGetError',U)();assert err==0,hex(err)
print('PASS: actual GLES shaders link and render; disabled/equal/not-equal/never/always RGB tests; one-level mip completeness')
api('eglMakeCurrent',U,V,V,V,V)(d,None,None,None)
api('eglDestroyContext',U,V,V)(d,ctx);api('eglDestroySurface',U,V,V)(d,surf);api('eglTerminate',U,V)(d)
