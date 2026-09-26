// The actual backend, not a duplicate GL implementation. Only the Android window is mocked.
#ifndef LCS_GLES_SOURCE
#define LCS_GLES_SOURCE "../../lcs/host/ge_gpu_backend_gles.cpp"
#endif
#include LCS_GLES_SOURCE
#include <cassert>
#include <iostream>
namespace lcs {
const LcsConfiguration &lcs_render_configuration(){static LcsConfiguration c;return c;}
void runtime_log_line(std::string_view){}
void runtime_log_error(std::string_view,std::string_view text){std::cerr<<text<<'\n';}
namespace android_host {
void set_source_size(std::uint32_t,std::uint32_t) noexcept{}
float ultrawide_x_scale() noexcept{return 1;}
}
}
using namespace lcs;
int main() {
 using GetDisplay=EGLDisplay(*)(EGLenum,void*,const EGLint*);
 auto get=reinterpret_cast<GetDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));assert(get);
 auto &s=state();s.display=get(0x31DD,nullptr,nullptr);
 assert(eglInitialize(s.display,nullptr,nullptr));assert(eglBindAPI(EGL_OPENGL_ES_API));
 const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 EGLint n=0;assert(eglChooseConfig(s.display,ca,&s.config,1,&n) && n);
 const EGLint pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
 s.surface=eglCreatePbufferSurface(s.display,s.config,pa);assert(s.surface!=EGL_NO_SURFACE);
 s.context=eglCreateContext(s.display,s.config,EGL_NO_CONTEXT,ctx);assert(s.context!=EGL_NO_CONTEXT);
 s.surface_dirty.store(false);s.enabled=true;s.scale=1;
 GeGpuDrawDescriptor d{};d.framebuffer_address=0x04010000;d.framebuffer_stride=16;d.framebuffer_format=3;
 d.scissor_x1=d.scissor_y1=15;d.primitive=3;
 d.depthbuffer_defined=true;d.depthbuffer_address=0x04090000;d.depthbuffer_stride=16;
 d.depth_test_enabled=true;d.depth_write_enabled=true;d.depth_function=6; // greater, cleared depth=0
 std::uint64_t epoch=0;
 auto draw=[&](float z,std::uint32_t rgba) {
     ge_gpu_backend_set_display_framebuffer(d.framebuffer_address,16,16);
     std::array<GeGpuVertex,6> v{};
     const float xy[6][2]={{0,0},{16,0},{16,16},{0,0},{16,16},{0,16}};
     for(unsigned i=0;i<6;i++){v[i].x=xy[i][0];v[i].y=xy[i][1];v[i].z=z;v[i].rgba=rgba;}
     ge_gpu_backend_accumulate_color_triangles(d,v);
     assert(ge_gpu_backend_finish_color_frame(++epoch));
 };
 auto read=[&] {
     const auto &t=s.targets.at(d.framebuffer_address&0x001ffff0u);
     glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
     std::array<std::uint8_t,4> p{};glReadPixels(8*s.scale,8*s.scale,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;
 };
 auto black=[&] {
     glBindFramebuffer(GL_FRAMEBUFFER,s.targets.at(d.framebuffer_address&0x001ffff0u).fbo);
     glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
     glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
 };
 draw(50000,0xff0000ffu);assert(read()[0]==255);
 d.framebuffer_address=0x04020000;draw(10000,0xffff0000u);
 if(read()[2]!=0) {std::cerr<<"FAIL: a new colour target lost the shared PSP depth contents\n";return 10;}
 d.depthbuffer_address=0x040a0000;draw(10000,0xffff0000u);assert(read()[2]==255);
 black();d.depthbuffer_address=0x04090000;draw(20000,0xff00ff00u);
 if(read()[1]!=0) {std::cerr<<"FAIL: switching back to a detached PSP depth buffer lost its contents\n";return 11;}
 // Both attachments detach from the first buffer, then return to it.
 d.framebuffer_address=0x04010000;d.depthbuffer_address=0x040a0000;draw(40000,0xffff0000u);
 black();d.depthbuffer_address=0x04090000;draw(30000,0xff00ff00u);assert(read()[1]==0);
 assert(glGetError()==GL_NO_ERROR);
 std::cout<<"PASS: real GLES shared Z buffer across colour targets, distinct Z buffers, and persistence after all attachments detach\n";
#ifndef LCS_BASELINE_TEST
 const auto reuses=s.batch_reuses;
 d.depth_test_enabled=false;d.depth_write_enabled=false;
 for(unsigned i=0;i<100;i++){draw(0,0xff46321eu);assert((read()==std::array<std::uint8_t,4>{30,50,70,255}));}
 assert(s.batch_reuses>=reuses+100 && s.batch_pool_bytes<=16u*1024u*1024u);
 assert(s.depth_reuses>=3);
 std::cout<<"PASS: 100 recycled CPU geometry batches retain exact pixels, capped pool and shared depth reuse\n";
#endif
 destroy_backend(s);
}
