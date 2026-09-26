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

    // Clip-space CPU-transformed vertices: compare GPU primitive clipping and
    // viewport against the old screen-space representation using real GLES.
    GeGpuDrawDescriptor color{};
    color.framebuffer_address=0x04010000;color.framebuffer_stride=16;
    color.framebuffer_format=3;color.scissor_x1=color.scissor_y1=15;color.primitive=3;
    GeGpuClipViewport vp{};
    assert(build_ge_gpu_clip_viewport(8,-8,32767,8,8,32767,true,vp));
    assert(!build_ge_gpu_clip_viewport(8,-8,32767,8.2f,8,32767,true,vp));
    assert(!build_ge_gpu_clip_viewport(8,-8,32767,8,8,32767,false,vp));
    assert(build_ge_gpu_clip_viewport(8,-8,32767,8,8,32767,true,vp));
    auto clear_target=[&] {
        glBindFramebuffer(GL_FRAMEBUFFER,s.targets.at(0x10000u).fbo);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    };
    auto read_all=[&] {
        std::array<std::uint8_t,16*16*4> pixels{};
        glBindFramebuffer(GL_FRAMEBUFFER,s.targets.at(0x10000u).fbo);
        glReadPixels(0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
    };
    for(unsigned test=0;test<120;test++) {
        const float shift=static_cast<float>(test%15)*0.07f;
        std::array<GeGpuVertex,3> clip{};
        clip[0].x=-1.3f+shift;clip[0].y=-0.7f;clip[0].z=0.2f;clip[0].w=1;
        clip[1].x=0.7f;clip[1].y=-0.5f;clip[1].z=0.1f;clip[1].w=1.2f;
        clip[2].x=-0.1f;clip[2].y=1.2f-shift;clip[2].z=-0.3f;clip[2].w=0.9f;
        for(auto &v:clip)v.rgba=0xff3f7fbfu;
        auto screen=clip;
        for(auto &v:screen) {
            v.x=v.x/v.w*8+8;v.y=v.y/v.w*-8+8;v.z=v.z/v.w*32767+32767;
        }
        clear_target();ge_gpu_backend_accumulate_color_triangles(color,screen);
        assert(ge_gpu_backend_finish_color_frame(3+test*2));const auto reference=read_all();
        clear_target();assert(ge_gpu_backend_accumulate_clip_vertices(color,vp,clip));
        assert(ge_gpu_backend_finish_color_frame(4+test*2));const auto actual=read_all();
        unsigned bad=0;
        for(unsigned i=0;i<actual.size();i++)if(std::abs(int(actual[i])-int(reference[i]))>1)bad++;
        assert(bad<=8); // Allow edge-coverage rounding only.
    }
    std::cout<<"PASS: 120 actual GLES clip/viewport images match screen-space reference\n";
    // Flat color is the provoking (last) vertex, and culling direction agrees
    // with the old screen-space area test. Depth-clip rejects out-of-frustum Z.
    std::array<GeGpuVertex,3> tri{};
    tri[0].x=-1;tri[0].y=-1;tri[1].x=1;tri[1].y=-1;tri[2].y=1;
    tri[0].rgba=0xff0000ff;tri[1].rgba=0xff00ff00;tri[2].rgba=0xffff0000;
    vp.flat_shading=true;
    clear_target();assert(ge_gpu_backend_accumulate_clip_vertices(color,vp,tri));
    assert(ge_gpu_backend_finish_color_frame(300));assert(pixel(8,8)[2]==255);
    vp.cull_enabled=true;vp.accept_counter_clockwise=true;
    clear_target();assert(ge_gpu_backend_accumulate_clip_vertices(color,vp,tri));
    assert(ge_gpu_backend_finish_color_frame(301));assert(pixel(8,8)[2]==0);
    vp.accept_counter_clockwise=false;
    clear_target();assert(ge_gpu_backend_accumulate_clip_vertices(color,vp,tri));
    assert(ge_gpu_backend_finish_color_frame(302));assert(pixel(8,8)[2]==255);
    vp.cull_enabled=false;
    for(auto &v:tri)v.z=2;
    clear_target();assert(ge_gpu_backend_accumulate_clip_vertices(color,vp,tri));
    assert(ge_gpu_backend_finish_color_frame(303));assert(pixel(8,8)[2]==0);
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS: actual flat shading, winding and depth clipping; no GL errors\n";

    destroy_backend(s);
}
