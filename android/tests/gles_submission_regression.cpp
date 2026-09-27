// Real GLES pixel/state oracle for partial uniform updates and compact batches.
#include <GLES3/gl3.h>
#include <cstdint>
static std::uint64_t uniform_calls{};
#define glUniform1i(...) (++uniform_calls,glUniform1i(__VA_ARGS__))
#define glUniform1f(...) (++uniform_calls,glUniform1f(__VA_ARGS__))
#define glUniform2f(...) (++uniform_calls,glUniform2f(__VA_ARGS__))
#define glUniform3i(...) (++uniform_calls,glUniform3i(__VA_ARGS__))
#define glUniform3f(...) (++uniform_calls,glUniform3f(__VA_ARGS__))
#include "../../lcs/host/ge_gpu_backend_gles.cpp"
#undef glUniform1i
#undef glUniform1f
#undef glUniform2f
#undef glUniform3i
#undef glUniform3f
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
    for(unsigned i=0;i<v.size();++i){v[i].x=xy[i][0];v[i].y=xy[i][1];v[i].u=xy[i][0]/4;v[i].v=xy[i][1]/4;v[i].rgba=0x8090b0d0;v[i].fog_factor=.35f;}
    return v;
}
using Pixels=std::array<std::uint8_t,16*16*4>;
Pixels pixels(){Pixels p{};glBindFramebuffer(GL_FRAMEBUFFER,state().targets.at(0x10000u).fbo);glReadPixels(0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
int main(){
    using GetDisplay=EGLDisplay(*)(EGLenum,void*,const EGLint*);
    auto get=reinterpret_cast<GetDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));assert(get);
    auto&s=state();s.display=get(0x31DD,nullptr,nullptr);
    assert(eglInitialize(s.display,nullptr,nullptr));assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLint n=0;assert(eglChooseConfig(s.display,ca,&s.config,1,&n)&&n);
    const EGLint pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    s.surface=eglCreatePbufferSurface(s.display,s.config,pa);s.context=eglCreateContext(s.display,s.config,EGL_NO_CONTEXT,ctx);
    assert(s.surface!=EGL_NO_SURFACE&&s.context!=EGL_NO_CONTEXT);s.surface_dirty=false;s.enabled=true;s.scale=1;
    ge_gpu_backend_set_display_framebuffer(0x04010000,16,16);
    GeGpuDrawDescriptor base{};base.framebuffer_address=0x04010000;base.framebuffer_stride=16;base.framebuffer_format=3;
    base.scissor_x1=base.scissor_y1=15;base.primitive=3;base.texture_address=0x08810000;
    base.texture_enabled=true;base.texture_function=3;base.texture_use_alpha=true;base.texture_format=3;
    base.texture_width=base.texture_height=base.texture_buffer_width=4;base.texture_content_signature=11;
    base.texture_mipmap_enabled=true;base.texture_max_level=2;base.texture_clamp_u=base.texture_clamp_v=true;
    base.texture_level_addresses[0]=base.texture_address;
    base.texture_level_widths[0]=base.texture_level_heights[0]=base.texture_level_buffer_widths[0]=4;
    ge_gpu_backend_prepare_texture_keys(base);ge_gpu_backend_record_draw(base);
    std::vector<std::byte> rgba;
    for(unsigned count:{16u,4u,1u})for(unsigned i=0;i<count;i++)for(unsigned b:{count==16u?220u:30u,count==4u?190u:50u,count==1u?160u:70u,128u})rgba.push_back(std::byte(b));
    assert(ge_gpu_backend_upload_decoded_texture_chain_packed(base,4,4,3,rgba));
    ge_gpu_backend_accumulate_color_triangles(base,quad());assert(ge_gpu_backend_finish_color_frame(1));

    // Independently force all uniform values into the actual shader, then
    // replay the same state sequence with incremental updates and compare
    // every output pixel. The sequence changes each cached uniform group.
    std::vector<GeGpuDrawDescriptor> sequence;
    auto d=base;
    for(unsigned repeat=0;repeat<5;repeat++)for(unsigned field=0;field<24;field++) {
        const auto on=(repeat&1u)==0;
        switch(field){
        case 0:d.texture_enabled=on;break;
        case 1:d.texture_function=repeat%5;break;
        case 2:d.texture_use_alpha=on;break;
        case 3:d.texture_double_color=on;break;
        case 4:d.texture_env=0x257fa1u+repeat*131;break;
        case 5:d.alpha_test_enabled=on;break;
        case 6:d.alpha_function=(repeat+3)%8;break;
        case 7:d.alpha_reference=repeat*50;break;
        case 8:d.alpha_mask=255u-repeat*17;break;
        case 9:d.fog_enabled=on;break;
        case 10:d.fog_color=0x5722aau+repeat*190;break;
        case 11:d.framebuffer_format=repeat%4;break;
        case 12:d.color_test_enabled=on;d.color_test_function=repeat%3;break;
        case 13:d.color_test_reference=0x708090u+repeat*255;break;
        case 14:d.color_test_mask=0xffffffu^(repeat*765);break;
        case 15:d.texture_level_mode=repeat%3;break;
        case 16:d.texture_level_offset16=static_cast<std::int8_t>(repeat*8-16);break;
        case 17:d.texture_max_level=repeat%3;break;
        case 18:d.texture_mipmap_enabled=on;break;
        case 19:d.clear_mode=on;break;
        case 20:d.clear_mode=false;d.texture_address=base.framebuffer_address;d.texture_width=d.texture_height=d.texture_buffer_width=16;d.texture_format=d.framebuffer_format;break;
        case 21:d.texture_width=8;break;
        case 22:d.texture_height=8;break;
        case 23:d.texture_address=base.texture_address;d.texture_width=d.texture_height=d.texture_buffer_width=4;d.texture_format=3;break;
        }
        // Keep the decoded image's layout identity stable while varying LOD
        // uniforms; feedback selection is still resolved from the address.
        d.texture_cache_key_hint=base.texture_cache_key_hint;
        sequence.push_back(d);
    }
    const auto uniform_state=[&]{
        std::vector<float> values;
        const auto integers=[&](GLint location,unsigned count){
            std::array<GLint,4> v{};if(location>=0)glGetUniformiv(s.program,location,v.data());
            for(unsigned i=0;i<count;i++)values.push_back(static_cast<float>(v[i]));
        };
        const auto floats=[&](GLint location,unsigned count){
            std::array<GLfloat,4> v{};if(location>=0)glGetUniformfv(s.program,location,v.data());
            values.insert(values.end(),v.begin(),v.begin()+count);
        };
        integers(s.u_tex,1);floats(s.u_feedback_scale,2);
        integers(s.u_texture_lod_mode,1);floats(s.u_texture_lod_bias,1);floats(s.u_texture_max_lod,1);
        integers(s.u_texture_flip_v,1);integers(s.u_color_test,1);
        integers(s.u_color_reference,3);integers(s.u_color_mask,3);
        integers(s.u_texture_enabled,1);integers(s.u_texture_function,1);
        integers(s.u_texture_use_alpha,1);integers(s.u_texture_double,1);floats(s.u_texture_env,3);
        integers(s.u_alpha_enabled,1);integers(s.u_alpha_func,1);integers(s.u_alpha_ref,1);integers(s.u_alpha_mask,1);
        integers(s.u_fog_enabled,1);floats(s.u_fog_color,3);integers(s.u_framebuffer_format,1);
        return values;
    };
    const auto render_sequence=[&](bool force_full,std::uint64_t &calls,
                                  std::vector<std::vector<float>> &uniforms){
        std::vector<Pixels> images;
        s.pixel_uniform_valid=false;calls=0;
        for(const auto &draw:sequence){
            glBindFramebuffer(GL_FRAMEBUFFER,s.targets.at(0x10000u).fbo);
            glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glColorMask(1,1,1,1);glDepthMask(1);
            glClearColor(.2f,.4f,.6f,.8f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glUseProgram(s.program);
            if(force_full)s.pixel_uniform_valid=false;
            const auto before=uniform_calls;
            set_pixel_uniforms(s,draw,draw.texture_enabled);
            calls+=uniform_calls-before;
            uniforms.push_back(uniform_state());
            ge_gpu_backend_accumulate_color_triangles(draw,quad());
            assert(ge_gpu_backend_finish_color_frame(2+images.size()));
            images.push_back(pixels());
        }
        assert(glGetError()==GL_NO_ERROR);return images;
    };
    std::uint64_t full_calls{},partial_calls{};
    std::vector<std::vector<float>> expected_uniforms,actual_uniforms;
    const auto reference=render_sequence(true,full_calls,expected_uniforms);
    const auto actual=render_sequence(false,partial_calls,actual_uniforms);
    assert(expected_uniforms==actual_uniforms); // Valid even when a fragment test discards pixels.
    assert(reference==actual);assert(partial_calls*4<full_calls);
    auto distinct=reference;std::sort(distinct.begin(),distinct.end());
    distinct.erase(std::unique(distinct.begin(),distinct.end()),distinct.end());
    assert(distinct.size()>4); // Oracle includes visible changes, not only discarded draws.
    std::cout<<"PASS: "<<sequence.size()<<" complete GLES images and all21 uniform readbacks equal forced-full oracle; controlled setter calls "<<full_calls<<" -> "<<partial_calls<<'\n';

    // The new compact input and retained legacy API must produce bit-identical
    // full-precision attributes and primitive indices, including merge seams.
    GeGpuClipViewport vp{};assert(build_ge_gpu_clip_viewport(8,-8,32767,8,8,32767,true,vp));
    for(unsigned primitive=3;primitive<=5;primitive++)for(bool flat:{false,true}){
        auto draw=base;draw.primitive=primitive;vp.flat_shading=flat;
        auto input=quad();for(auto &v:input){v.x=(v.x-8)/8;v.y=(8-v.y)/8;v.z=.125f;v.w=1.25f;v.q=.75f;}
        std::vector<GeGpuClipVertex> compact(input.begin(),input.end());
        assert(ge_gpu_backend_accumulate_clip_vertices(draw,vp,input));
        assert(ge_gpu_backend_accumulate_clip_vertices(draw,vp,input));
        assert(s.batches.size()==1);const auto expected=s.batches[0];recycle_batches(s);
        assert(ge_gpu_backend_accumulate_clip_vertices(draw,vp,compact));
        assert(ge_gpu_backend_accumulate_clip_vertices(draw,vp,compact));
        assert(s.batches.size()==1);const auto &got=s.batches[0];
        assert(expected.indices==got.indices && expected.vertices.size()==got.vertices.size());
        assert(std::memcmp(expected.vertices.data(),got.vertices.data(),got.vertices.size()*sizeof(GeGpuClipVertex))==0);
        assert(sizeof(got.vertices[0])==36);recycle_batches(s);
    }
    vp.requires_inside_depth=true;std::array<GeGpuClipVertex,3> outside{};outside[0].z=2;
    assert(!ge_gpu_backend_accumulate_clip_vertices(base,vp,outside)&&s.batches.empty());
    std::cout<<"PASS: compact/legacy clip batches match exact 36-byte attributes and list/strip/fan indices, flat colors, merge seams and depth rejection\n";
    destroy_backend(s);
}
