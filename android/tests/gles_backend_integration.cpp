// Real GLES backend on Mesa; only the Android native-window hooks are mocked.
#include "../../lcs/host/ge_gpu_backend_gles.cpp"
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
std::array<GeGpuVertex,6> quad(float x0,float y0,float x1,float y1,float tw=1,float th=1){
    std::array<GeGpuVertex,6> v{};
    const float a[6][4]={{x0,y0,0,0},{x1,y0,tw,0},{x1,y1,tw,th},{x0,y0,0,0},{x1,y1,tw,th},{x0,y1,0,th}};
    for(int i=0;i<6;i++){v[i].x=a[i][0];v[i].y=a[i][1];v[i].u=a[i][2];v[i].v=a[i][3];}return v;
}
std::array<unsigned char,4> pixel(int x,int y){
    glBindFramebuffer(GL_FRAMEBUFFER,state().targets.at(0x10000u).fbo);
    std::array<unsigned char,4> p{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;
}
int main(){
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
    ge_gpu_backend_set_display_framebuffer(0x04010000,16,16);
    GeGpuDrawDescriptor d{};d.texture_enabled=true;d.texture_function=3;d.texture_use_alpha=true;
    d.texture_address=0x08810000;d.texture_width=d.texture_height=d.texture_buffer_width=1;
    d.texture_format=3;d.framebuffer_address=0x04010000;d.framebuffer_stride=16;d.framebuffer_format=3;
    d.scissor_x1=d.scissor_y1=15;d.texture_clamp_u=d.texture_clamp_v=true;d.texture_content_signature=101;
    ge_gpu_backend_prepare_texture_keys(d);ge_gpu_backend_record_draw(d);
    const std::array<std::byte,4> red{std::byte{255},std::byte{0},std::byte{0},std::byte{255}},blue{std::byte{0},std::byte{0},std::byte{255},std::byte{255}};
    assert(ge_gpu_backend_upload_decoded_texture(d,1,1,red));d.texture_content_signature=0;
    assert(!ge_gpu_backend_texture_signature_needed(d));ge_gpu_backend_accumulate_color_triangles(d,quad(0,0,8,16));
    android_gles_texture_barrier();assert(ge_gpu_backend_texture_signature_needed(d));
    d.texture_content_signature=202;ge_gpu_backend_record_draw(d);assert(ge_gpu_backend_upload_decoded_texture(d,1,1,blue));
    d.texture_content_signature=0;ge_gpu_backend_accumulate_color_triangles(d,quad(8,0,16,16));
    assert(ge_gpu_backend_finish_color_frame(1));
    assert((pixel(3,8)==std::array<unsigned char,4>{255,0,0,255}));assert((pixel(12,8)==std::array<unsigned char,4>{0,0,255,255}));
    std::cout<<"PASS: actual backend preserves red/blue versions across a texture barrier\n";
    d.texture_address=0x04010000;d.texture_width=d.texture_height=d.texture_buffer_width=16;d.texture_content_signature=0;
    ge_gpu_backend_prepare_texture_keys(d);ge_gpu_backend_record_draw(d);
    ge_gpu_backend_accumulate_color_triangles(d,quad(0,0,16,16,16,16));assert(ge_gpu_backend_finish_color_frame(2));
    assert((pixel(3,8)==std::array<unsigned char,4>{255,0,0,255}));assert((pixel(12,8)==std::array<unsigned char,4>{0,0,255,255}));
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS: actual self-feedback, sampler cache and frame staging; no GL errors\n";
    destroy_backend(s);
}
