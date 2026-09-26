// Exercise the real GLES backend; only the Android window/configuration hooks
// are replaced. Keep this test self-contained so a clean checkout can run it.
#include "../../lcs/host/ge_gpu_backend_gles.cpp"
#include <cassert>
#include <filesystem>
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
std::vector<lcs::GeGpuVertex> vertices(std::uint32_t color,float depth){
    const float xy[6][2]={{0,0},{16,0},{16,16},{0,0},{16,16},{0,16}};
    std::vector<lcs::GeGpuVertex> result(6);
    for(unsigned i=0;i<6;++i){result[i].x=xy[i][0];result[i].y=xy[i][1];
        result[i].z=depth;result[i].rgba=color;}
    return result;
}
void render(lcs::GlesState&,const lcs::GeGpuDrawDescriptor&draw,
            const std::vector<lcs::GeGpuVertex>&data){
    static std::uint64_t epoch=0;
    lcs::ge_gpu_backend_set_display_framebuffer(draw.framebuffer_address,16,16);
    lcs::ge_gpu_backend_accumulate_color_triangles(draw,data);
    assert(lcs::ge_gpu_backend_finish_color_frame(++epoch));
}
std::vector<std::byte> read_target(lcs::GlesState&s,std::uint32_t addr){
    auto &t=s.targets.at(addr&0x001ffff0u);glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
    std::vector<std::byte>rgba(t.render_width*t.render_height*4);
    glReadPixels(0,0,t.render_width,t.render_height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());return rgba;
}
int main(int argc,char**argv){
    assert(argc==2);
    using GetDisplay=EGLDisplay(*)(EGLenum,void*,const EGLint*);
    auto get=reinterpret_cast<GetDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));assert(get);
    auto &s=lcs::state();s.display=get(0x31DD,nullptr,nullptr);assert(eglInitialize(s.display,nullptr,nullptr));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLint n=0;assert(eglChooseConfig(s.display,attributes,&s.config,1,&n)&&n);
    const EGLint pbuffer[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    s.surface=eglCreatePbufferSurface(s.display,s.config,pbuffer);
    const EGLint context[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    s.context=eglCreateContext(s.display,s.config,EGL_NO_CONTEXT,context);
    assert(eglMakeCurrent(s.display,s.surface,s.surface,s.context));
    s.enabled=true;s.scale=1;s.surface_dirty=false;s.gl_thread=std::this_thread::get_id();
    s.display_logical_width=16;s.display_logical_height=16;
    std::string error;assert(lcs::create_gl_objects(s,error));
    lcs::GeGpuDrawDescriptor source{};source.framebuffer_address=0x041a0000;source.framebuffer_format=3;
    source.framebuffer_stride=16;source.primitive=3;
    source.scissor_x1=15;source.scissor_y1=15;source.depth_test_enabled=false;source.depth_write_enabled=false;
    render(s,source,vertices(0xff0000ff,0));
    auto lower=vertices(0xffff0000,0);for(auto&v:lower)v.y=8+v.y/2;
    render(s,source,lower);
    const auto reference=read_target(s,source.framebuffer_address);
    auto copy=source;copy.framebuffer_address=0x041b0000;copy.texture_address=source.framebuffer_address;
    copy.texture_enabled=true;copy.texture_width=32;copy.texture_height=32;copy.texture_function=3;
    copy.texture_buffer_width=16;copy.texture_use_alpha=true;copy.texture_clamp_u=true;copy.texture_clamp_v=true;
    // Public accumulation normalizes PSP texel coordinates by the declared
    // texture extent; the shader must then map them to the 16x16 target.
    auto vs=vertices(0xffffffff,0);for(auto&v:vs){v.u=v.x;v.v=v.y;}
    render(s,copy,vs);
    assert(read_target(s,copy.framebuffer_address)==reference);
    std::cout<<"PASS: actual backend copies complete 16x16 framebuffer through a declared 32x32 texture without a half-image crop\n";
    auto image=copy;image.texture_address=0x088a0000;image.texture_width=2;image.texture_height=2;
    image.texture_content_signature=915;image.texture_format=3;
    const std::array<std::byte,16>rgba{std::byte{255},std::byte{0},std::byte{0},std::byte{255},
        std::byte{0},std::byte{255},std::byte{0},std::byte{255},
        std::byte{0},std::byte{0},std::byte{255},std::byte{255},
        std::byte{255},std::byte{255},std::byte{255},std::byte{255}};
    assert(lcs::ge_gpu_backend_upload_decoded_texture(image,2,2,rgba));
    lcs::GlesBatch batch;batch.draw=image;batch.vertices=vertices(0xffffffff,0);
    s.batches.push_back(batch);
    assert(lcs::render_capture_request(argv[1]));lcs::render_capture_frame_boundary();lcs::render_capture_next_draw();
    GLint previous=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);
    lcs::capture_gpu_frame(s);GLint restored=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&restored);
    assert(previous==restored);lcs::render_capture_finish();assert(lcs::render_capture_status()==3);
    assert(std::filesystem::is_regular_file(argv[1]));assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS: actual GPU-cache readback and vertex/descriptor capture; framebuffer binding restored\n";
    lcs::destroy_backend(s);
}
