// Run raw PSP through vertices through render_ge_primitive and the real GLES
// backend. Captured 0.6.17 draw1069's 64x64 reflection was shifted entirely
// beyond its REGION2 by Android's display-centered HUD correction.
#include "../../lcs/host/ge_gpu_backend_gles.cpp"
#include "../../lcs/host/ge_renderer.hpp"
#include <cassert>
#include <iostream>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return 0; }
const char *runtime_thread_name() noexcept { return "offscreen-hud-test"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}
namespace lcs {
const LcsConfiguration &lcs_render_configuration() {
    static LcsConfiguration c=[] { LcsConfiguration v; v.initialized=true;
        v.display.hud_scale=0.7f; v.rendering.hardware_transform=false; return v; }();
    return c;
}
void runtime_log_line(std::string_view) {}
void runtime_log_error(std::string_view,std::string_view text) { std::cerr<<text<<'\n'; }
bool lcs_camera_hook_enabled() noexcept { return false; }
bool lcs_camera_in_use() noexcept { return false; }
void fps_overlay_observe_draw(const GeGpuDrawDescriptor &,std::uint32_t) noexcept {}
namespace android_host {
void set_source_size(std::uint32_t,std::uint32_t) noexcept {}
std::uint32_t source_width() noexcept { return 512; }
std::uint32_t source_height() noexcept { return 320; }
float ultrawide_x_scale() noexcept { return 0.73846153846f; }
float geometry_x_scale() noexcept { return 1; }
}
}
using namespace lcs;
namespace {
constexpr std::uint32_t scene=0x04088000u, reflection=0x04044000u, vertex_address=0x08001000u;
void finish() { static unsigned frame=0; assert(ge_gpu_backend_finish_color_frame(++frame)); }
void fill(std::uint32_t address,unsigned width,unsigned height,std::uint32_t color,unsigned format=3) {
    GeGpuDrawDescriptor d{}; d.framebuffer_address=address; d.framebuffer_stride=width;
    d.framebuffer_format=format; d.primitive=3; d.region_defined=true;
    d.region_x1=width-1; d.region_y1=height-1; d.scissor_x1=width; d.scissor_y1=height;
    const float xy[6][2]={{0,0},{float(width),0},{float(width),float(height)},
        {0,0},{float(width),float(height)},{0,float(height)}};
    std::array<GeGpuVertex,6> v{};
    for(unsigned i=0;i<6;++i) { v[i].x=xy[i][0];v[i].y=xy[i][1];v[i].rgba=color; }
    ge_gpu_backend_record_draw(d);ge_gpu_backend_accumulate_color_triangles(d,v);finish();
}
std::array<std::uint8_t,4> pixel(std::uint32_t address,unsigned x,unsigned y) {
    const auto &t=state().targets.at(address&0x001ffff0u);
    glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
    std::array<std::uint8_t,4> p{};glReadPixels(x,t.render_height-y-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;
}
std::array<std::uint32_t,256> commands(std::uint32_t address,unsigned width,unsigned height) {
    std::array<std::uint32_t,256> c{};
    c[0x12]=0x800102; // captured u16 UV + s16 XYZ through layout, stride10
    c[0x15]=0x15000000;c[0x16]=(width-1)|((height-1)<<10);
    c[0x9c]=address&0x1ffff0;c[0x9d]=width;
    c[0x9e]=0x180000;c[0x9f]=width;
    c[0xd2]=width==64?0:3;c[0xd4]=0;c[0xd5]=width|(height<<10);c[0xe7]=1;
    c[0x55]=0xffffff;c[0x58]=255;c[0xc9]=0x103;
    return c;
}
void texture(std::array<std::uint32_t,256>&c,std::uint32_t address,unsigned log_width,unsigned log_height,unsigned stride,unsigned format) {
    c[0x1e]=1;c[0xa0]=address&0xffffff;c[0xa8]=(address>>8&0xf0000)|stride;
    c[0xb8]=log_width|(log_height<<8);c[0xc3]=format;c[0xc7]=0x101;
}
void draw(psprecomp::GuestMemory &memory,const std::array<std::uint32_t,256>&c,
    const GeTransformState &transform,float x0,float y0,float x1,float y1,
    unsigned u0=0,unsigned v0=0,unsigned u1=64,unsigned v1=64,unsigned depth=0) {
    const std::array<std::uint16_t,10> raw{std::uint16_t(u0),std::uint16_t(v0),std::uint16_t(x0),std::uint16_t(y0),std::uint16_t(depth),
        std::uint16_t(u1),std::uint16_t(v1),std::uint16_t(x1),std::uint16_t(y1),std::uint16_t(depth)};
    for(unsigned i=0;i<raw.size();++i)memory.store16(vertex_address+i*2,raw[i]);
    GeRenderStats stats;std::string error;
    assert(render_ge_primitive(memory,c,transform,vertex_address,0,(6u<<16)|2u,stats,error));
    assert(!state().batches.empty());
}
std::pair<float,float> x_bounds() {
    float lo=10000,hi=-10000;
    for(const auto&v:state().batches.back().vertices){lo=std::min(lo,v.x);hi=std::max(hi,v.x);}return {lo,hi};
}
}
int main() {
    using GetDisplay=EGLDisplay(*)(EGLenum,void*,const EGLint*);
    auto get=reinterpret_cast<GetDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));assert(get);
    auto &s=state();s.display=get(0x31DD,nullptr,nullptr);assert(eglInitialize(s.display,nullptr,nullptr));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint config[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLint n=0;assert(eglChooseConfig(s.display,config,&s.config,1,&n)&&n);
    const EGLint surface[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE},context[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    s.surface=eglCreatePbufferSurface(s.display,s.config,surface);s.context=eglCreateContext(s.display,s.config,EGL_NO_CONTEXT,context);
    assert(s.surface!=EGL_NO_SURFACE&&s.context!=EGL_NO_CONTEXT);s.surface_dirty=false;s.enabled=true;s.scale=1;
    ge_gpu_backend_set_display_framebuffer(scene,512,320);
    fill(scene,512,320,0xff0000ff);fill(reflection,64,64,0xff00ff00,0);
    psprecomp::GuestMemory memory;
    GeTransformState transform{};reset_ge_transform_state(transform);
    auto env=commands(reflection,64,64);texture(env,scene,9,9,512,3);
    draw(memory,env,transform,0,0,64,64,85,0,426,320);
    auto [lo,hi]=x_bounds();
    if(lo!=0 || hi!=64) { std::cerr<<"FAIL: reflection map shifted from 0..64 to "<<lo<<".."<<hi<<'\n';return 31; }
    finish();assert((pixel(reflection,32,32)==std::array<std::uint8_t,4>{255,0,0,255}));
    // The captured highlight is a RAM T4 sprite, so a feedback-only exclusion
    // cannot fix it. A HUD scale below1 must not shrink that offscreen sprite.
    auto palette=std::make_shared<GeClutState::Bytes>();palette->fill(0);
    (*palette)[4]=0;(*palette)[5]=0;(*palette)[6]=255;(*palette)[7]=255;transform.clut.snapshot=palette;
    for(unsigned i=0;i<2048;++i)memory.store8(0x08004000+i,0x11);
    auto highlight=env;texture(highlight,0x08004000,6,6,64,4);highlight[0xc5]=0xff03;
    draw(memory,highlight,transform,4,4,12,12);
    assert(x_bounds()==std::pair(4.f,12.f));finish();
    assert((pixel(reflection,8,8)==std::array<std::uint8_t,4>{0,0,255,255}));
    assert((pixel(reflection,3,8)==std::array<std::uint8_t,4>{255,0,0,255}));
    // A screen-sized second backbuffer still receives narrow-HUD correction.
    auto hud=commands(0x04100000,512,320);
    draw(memory,hud,transform,32,20,48,36);
    auto [hud_lo,hud_hi]=x_bounds();assert(hud_lo!=32 && hud_hi-hud_lo<16);finish();
    // Full-screen fades/backgrounds retain coverage; a narrow scissor must
    // not classify an otherwise full-size screen target as an offscreen map.
    draw(memory,hud,transform,0,0,512,320);assert(x_bounds()==std::pair(0.f,512.f));finish();
    hud[0xd4]=20|(20<<10);hud[0xd5]=100|(100<<10);
    draw(memory,hud,transform,32,20,48,36);assert(x_bounds().first==hud_lo);finish();
    // Through world effects (captured draws1285..1289/1300) read scene depth
    // without writing it. Keep the same exclusion as the general HUD scaler.
    hud[0x23]=1;hud[0xe7]=1;hud[0xde]=7;
    draw(memory,hud,transform,32,20,48,36,0,0,64,64,10874);assert(x_bounds()==std::pair(32.f,48.f));finish();
    hud[0xe7]=0;
    draw(memory,hud,transform,32,20,48,36);assert(x_bounds().first==hud_lo);finish();
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS: actual renderer keeps reflection feedback and T4 highlight inside offscreen64, "
        "with ultrawide display and HUD scale0.7; doublebuffer HUD, fullscreen, narrow-scissor and world-depth controls retained\n";
    destroy_backend(s);
}
