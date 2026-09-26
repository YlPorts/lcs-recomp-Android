// Real Mesa EGL/GLES regression for texture decoding across Android surface replacement.
// Only native window creation is substituted with a pbuffer; context, textures,
// queued batches, uploads, frame execution and presentation use production code.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
static bool reject_next_upload{};
static void test_texture_image(GLenum target, GLint level, GLint internalformat,
    GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    if (reject_next_upload) { reject_next_upload=false; width=-1; }
    glTexImage2D(target,level,internalformat,width,height,border,format,type,pixels);
}
#include <android/native_window.h>
static EGLSurface test_window_surface(EGLDisplay display,EGLConfig config,EGLNativeWindowType,const EGLint*) {
    const EGLint pbuffer[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    return eglCreatePbufferSurface(display,config,pbuffer);
}
#define eglCreateWindowSurface test_window_surface
#define glTexImage2D test_texture_image
#include "../../lcs/host/ge_gpu_backend_gles.cpp"
#undef eglCreateWindowSurface
#undef glTexImage2D
#include <cassert>
#include <iostream>
namespace lcs {
const LcsConfiguration &lcs_render_configuration(){static LcsConfiguration c;return c;}
void runtime_log_line(std::string_view){}
void runtime_log_error(std::string_view,std::string_view text){std::cerr<<text<<'\n';}
namespace android_host {
void set_source_size(std::uint32_t,std::uint32_t) noexcept{}
float ultrawide_x_scale() noexcept{return 1;}
float geometry_x_scale() noexcept{return 1;}
}
}
using namespace lcs;
std::array<GeGpuVertex,6> quad(){
    std::array<GeGpuVertex,6> v{};
    const float xy[6][2]={{0,0},{16,0},{16,16},{0,0},{16,16},{0,16}};
    for(int i=0;i<6;i++){v[i].x=xy[i][0];v[i].y=xy[i][1];v[i].rgba=0xffffffff;}
    return v;
}
std::array<unsigned char,4> pixel(int x=8){
    glBindFramebuffer(GL_FRAMEBUFFER,state().targets.at(0x10000u).fbo);
    std::array<unsigned char,4> p{};glReadPixels(x,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;
}
int main(){
    using GetDisplay=EGLDisplay(*)(EGLenum,void*,const EGLint*);
    auto get=reinterpret_cast<GetDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));assert(get);
    auto&s=state();s.display=get(0x31DD,nullptr,nullptr);
    assert(eglInitialize(s.display,nullptr,nullptr));assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLint n=0;assert(eglChooseConfig(s.display,ca,&s.config,1,&n)&&n);
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    s.surface=test_window_surface(s.display,s.config,nullptr,nullptr);
    s.context=eglCreateContext(s.display,s.config,EGL_NO_CONTEXT,ctx);assert(s.context!=EGL_NO_CONTEXT);
    ANativeWindow first{},second{};s.window=&first;
    s.surface_dirty=false;s.enabled=true;s.scale=1;
    ge_gpu_backend_set_display_framebuffer(0x04010000,16,16);
    GeGpuDrawDescriptor d{};d.texture_enabled=true;d.texture_function=3;d.texture_use_alpha=true;
    d.texture_address=0x08810000;d.texture_width=d.texture_height=d.texture_buffer_width=1;
    d.texture_format=3;d.framebuffer_address=0x04010000;d.framebuffer_stride=16;d.framebuffer_format=3;
    d.scissor_x1=d.scissor_y1=15;d.texture_clamp_u=d.texture_clamp_v=true;d.texture_content_signature=101;d.primitive=3;
    ge_gpu_backend_prepare_texture_keys(d);ge_gpu_backend_record_draw(d);
    const std::array<std::byte,4> red{std::byte{255},std::byte{0},std::byte{0},std::byte{255}},blue{std::byte{0},std::byte{0},std::byte{255},std::byte{255}};
    assert(ge_gpu_backend_upload_decoded_texture(d,1,1,red));
    const auto context=s.context;const auto image=s.textures.at(texture_lookup_key(d)).id;
    ge_gpu_backend_set_native_window(&second);assert(ge_gpu_backend_graphics_ready());
    assert(s.context==context&&s.textures.at(texture_lookup_key(d)).id==image&&glIsTexture(image));
    ge_gpu_backend_accumulate_color_triangles(d,quad());assert(ge_gpu_backend_finish_color_frame(1));
    assert((pixel()==std::array<unsigned char,4>{255,0,0,255}));
    std::cout<<"PASS: actual surface replacement preserves EGL context, existing GL image ID and red pixels\n";
    auto fresh=d;fresh.texture_address+=4;fresh.texture_content_signature=202;
    ge_gpu_backend_prepare_texture_keys(fresh);ge_gpu_backend_record_draw(fresh);
    ge_gpu_backend_set_native_window(nullptr);
    assert(ge_gpu_backend_upload_decoded_texture(fresh,1,1,blue));
    assert(!ge_gpu_backend_graphics_ready());
    assert(!ge_gpu_backend_texture_needed(fresh));
    assert(ge_gpu_backend_texture_available(fresh));
    assert(s.pending_textures.size()==1 && s.pending_texture_bytes==4);
    // Repeated decode requests for the same content do not grow the queue.
    assert(ge_gpu_backend_upload_decoded_texture(fresh,1,1,blue));
    assert(s.pending_textures.size()==1 && s.pending_texture_bytes==4);
    auto left=quad();for(auto &v:left)v.x/=3.0f;
    ge_gpu_backend_accumulate_color_triangles(fresh,left);
    auto version_b=fresh;version_b.texture_content_signature=203;
    ge_gpu_backend_record_draw(version_b);
    const std::array<std::byte,4> green{std::byte{0},std::byte{255},std::byte{0},std::byte{255}};
    assert(ge_gpu_backend_upload_decoded_texture(version_b,1,1,green));
    auto right=quad();for(auto &v:right)v.x=16.0f/3.0f+v.x/3.0f;
    ge_gpu_backend_accumulate_color_triangles(version_b,right);
    ge_gpu_backend_record_draw(fresh);
    auto again=quad();for(auto &v:again)v.x=32.0f/3.0f+v.x/3.0f;
    ge_gpu_backend_accumulate_color_triangles(fresh,again);
    assert(s.pending_textures.size()==2);
    // A realistic first-scene burst remains staged without extra decodes.
    for(unsigned i=0;i<127;i++) {
        auto burst=d;burst.texture_address=0x08900000+i*16;burst.texture_content_signature=300+i;
        ge_gpu_backend_prepare_texture_keys(burst);ge_gpu_backend_record_draw(burst);
        assert(ge_gpu_backend_upload_decoded_texture(burst,1,1,blue));
        assert(!ge_gpu_backend_texture_needed(burst));
    }
    assert(s.pending_textures.size()==129);
    const auto uploads=s.report.decoded_texture_uploads,presents=s.perf_presents;
    ge_gpu_backend_set_native_window(&first);assert(ge_gpu_backend_graphics_ready());
    assert(s.context==context && s.textures.at(texture_lookup_key(d)).id==image);
    assert(s.pending_textures.empty() && s.pending_texture_bytes==0);
    assert(s.report.decoded_texture_uploads==uploads+129);
    assert(ge_gpu_backend_texture_available(fresh));
    assert(ge_gpu_backend_finish_color_frame(2));
    assert((pixel(2)==std::array<unsigned char,4>{0,0,255,255}));
    assert((pixel(8)==std::array<unsigned char,4>{0,255,0,255}));
    assert((pixel(14)==std::array<unsigned char,4>{0,0,255,255}));
    assert(s.perf_missing_textures==0 && s.perf_presents==presents+1);
    std::cout<<"PASS: first resumed draw uploads 129 staged textures, preserves queued same-address versions, no missing texture or extra blank frame\n";

    // An unknown content signature must retain its exact cache key, even if
    // a later draw establishes a signed version at the same guest address.
    ge_gpu_backend_set_native_window(nullptr);
    auto unknown=d;unknown.texture_address=0x08a10000;unknown.texture_content_signature=0;
    ge_gpu_backend_prepare_texture_keys(unknown);ge_gpu_backend_record_draw(unknown);
    const auto unknown_key=texture_lookup_key(unknown);
    assert(ge_gpu_backend_upload_decoded_texture(unknown,1,1,blue));
    auto known=unknown;known.texture_content_signature=777;
    ge_gpu_backend_record_draw(known);assert(ge_gpu_backend_upload_decoded_texture(known,1,1,green));
    const auto known_key=texture_lookup_key(known);
    ge_gpu_backend_set_native_window(&second);
    assert(ge_gpu_backend_graphics_ready());
    assert(unknown_key!=known_key && s.textures.contains(unknown_key) && s.textures.contains(known_key));
    assert(s.textures.at(unknown_key).signature==0 && s.textures.at(known_key).signature==777);
    for(auto key:{unknown_key,known_key}) {
        GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.textures.at(key).id,0);
        std::array<unsigned char,4> p{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());
        assert((p==(key==unknown_key?std::array<unsigned char,4>{0,0,255,255}:std::array<unsigned char,4>{0,255,0,255})));
        glDeleteFramebuffers(1,&fbo);
    }
    std::cout<<"PASS: unknown→known content signature preserves both exact staged image keys/pixels\n";

    ge_gpu_backend_set_native_window(nullptr);
    auto retry=d;retry.texture_address=0x08a20000;retry.texture_content_signature=778;
    ge_gpu_backend_prepare_texture_keys(retry);ge_gpu_backend_record_draw(retry);
    assert(ge_gpu_backend_upload_decoded_texture(retry,1,1,blue));
    ge_gpu_backend_set_native_window(&first);reject_next_upload=true;
    assert(!ge_gpu_backend_graphics_ready());
    assert(s.pending_textures.size()==1 && s.pending_texture_bytes==4);
    assert(!s.textures.contains(texture_lookup_key(retry)));
    assert(ge_gpu_backend_graphics_ready());
    assert(s.pending_textures.empty() && s.pending_texture_bytes==0);
    // Automatic retry changed no pixels or presentation count.
    assert(s.perf_presents==presents+1 && s.perf_missing_textures==0);
    assert((pixel(2)==std::array<unsigned char,4>{0,0,255,255}));
    std::cout<<"PASS: injected GL upload failure retains bytes and retries before readiness without presenting\n";

    // Bad mip payloads must not be advertised as a deferred cache hit.
    ge_gpu_backend_set_native_window(nullptr);
    auto malformed=d;malformed.texture_address=0x08a00000;
    ge_gpu_backend_prepare_texture_keys(malformed);
    assert(!ge_gpu_backend_upload_decoded_texture_chain_packed(malformed,2,2,2,std::vector<std::byte>(16)));
    assert(s.pending_textures.empty());

    // Entry-budget rejection suppresses the entire incomplete frame; it may
    // not turn a missing required image into a visible white fallback.
    for(unsigned i=0;i<kPendingTextureEntryLimit;i++) {
        auto t=d;t.texture_address=0x08b00000+i*16;t.texture_content_signature=5000+i;
        ge_gpu_backend_prepare_texture_keys(t);ge_gpu_backend_record_draw(t);
        assert(ge_gpu_backend_upload_decoded_texture(t,1,1,blue));
    }
    auto rejected=d;rejected.texture_address=0x08c00000;rejected.texture_content_signature=9000;
    ge_gpu_backend_prepare_texture_keys(rejected);ge_gpu_backend_record_draw(rejected);
    assert(!ge_gpu_backend_upload_decoded_texture(rejected,1,1,red));
    assert(s.pending_textures.size()==kPendingTextureEntryLimit);
    assert(s.texture_upload_frame_incomplete);
    ge_gpu_backend_accumulate_color_triangles(rejected,quad());
    const auto before_rejected=s.perf_presents;
    ge_gpu_backend_set_native_window(&second);assert(ge_gpu_backend_graphics_ready());
    assert(!ge_gpu_backend_finish_color_frame(3));
    assert(s.batches.empty() && !s.texture_upload_frame_incomplete);
    assert(s.perf_presents==before_rejected && s.perf_missing_textures==0);
    assert((pixel(2)==std::array<unsigned char,4>{0,0,255,255}));
    assert(ge_gpu_backend_texture_needed(rejected));
    assert(ge_gpu_backend_upload_decoded_texture(rejected,1,1,red));
    ge_gpu_backend_accumulate_color_triangles(rejected,quad());
    assert(ge_gpu_backend_finish_color_frame(4));
    assert((pixel()==std::array<unsigned char,4>{255,0,0,255}));
    std::cout<<"PASS: entry cap skips incomplete batch without swapping; following complete frame retries and renders correctly\n";

    ge_gpu_backend_set_native_window(nullptr);
    for(unsigned i=0;i<2;i++) {
        auto large=d;large.texture_address=0x08d00000+i*16;
        large.texture_width=large.texture_height=large.texture_buffer_width=2048;
        large.texture_content_signature=10000+i;ge_gpu_backend_prepare_texture_keys(large);
        assert(ge_gpu_backend_upload_decoded_texture_chain_packed(large,2048,2048,1,
            std::vector<std::byte>(16u*1024u*1024u)));
    }
    assert(s.pending_texture_bytes==kPendingTextureByteLimit);
    auto overflow=d;overflow.texture_address=0x08e00000;ge_gpu_backend_prepare_texture_keys(overflow);
    assert(!ge_gpu_backend_upload_decoded_texture(overflow,1,1,red));
    assert(s.pending_texture_bytes==kPendingTextureByteLimit && s.pending_textures.size()==2);
    // Shutdown while detached still frees all retained CPU pixels.
    destroy_backend(s);
    assert(s.pending_textures.empty() && s.pending_texture_bytes==0);
    std::cout<<"PASS: byte cap is bounded at 32 MiB and detached shutdown clears staged images\n";
}
