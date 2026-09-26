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
extern "C" std::uint32_t lcs_reference_blend(std::uint32_t,std::uint32_t,
    unsigned,unsigned,unsigned,std::uint32_t,std::uint32_t,std::uint32_t);
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
    assert(build_ge_gpu_clip_viewport(8,-8,32767,8,8,32767,false,vp));
    assert(vp.requires_inside_depth);
    {
        std::array<GeGpuVertex,3> v{};
        v[0].z=2.0f; // outside [-w,w]: CPU fallback must stay active.
        const auto before=s.batches.size();
        assert(!ge_gpu_backend_accumulate_clip_vertices(color,vp,v));
        assert(s.batches.size()==before);
        v[0].z=0.5f;
        assert(ge_gpu_backend_accumulate_clip_vertices(color,vp,v));
        s.batches.clear();
    }
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
        clear_target();
        vp.requires_inside_depth=(test & 1u)!=0u;
        assert(ge_gpu_backend_accumulate_clip_vertices(color,vp,clip));
        assert(ge_gpu_backend_finish_color_frame(4+test*2));const auto actual=read_all();
        unsigned bad=0;
        for(unsigned i=0;i<actual.size();i++)if(std::abs(int(actual[i])-int(reference[i]))>1)bad++;
        assert(bad<=8); // Allow edge-coverage rounding only.
    }
    std::cout<<"PASS: 120 actual GLES clip/viewport images match screen-space reference\n";
    vp.requires_inside_depth=false;
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


    // Compare explicit indices and adjacent merging against native GL lists,
    // strips and fans (the previous path), not against a second use of the new converter.
    unsigned merged_images=0;
    for (unsigned primitive=3;primitive<=5;++primitive)
    for (unsigned test=0;test<40;++test) {
        auto d=color;d.primitive=primitive;
        auto viewport=vp;viewport.flat_shading=(test&1)!=0;
        viewport.cull_enabled=(test%3)!=0;
        viewport.accept_counter_clockwise=(test%3)==2;
        d.blend_enabled=(test%4)==0;d.blend_equation=0;
        d.blend_source_factor=2;d.blend_dest_factor=3;
        std::array<GeGpuVertex,6> a{},b{};
        for(unsigned n=0;n<a.size();++n) {
            a[n].x=-.95f+float(n%2)*.9f+float(test%5)*.03f;
            a[n].y=-.9f+float(n/2)*.6f;
            a[n].w=1.0f;a[n].z=.2f;
            a[n].rgba=0x800000ffu+(n*35u<<8)+(n*19u<<16);
            b[n]=a[n];b[n].x+=.9f;b[n].y-=.15f;b[n].rgba^=0x00ffffffu;
        }
        clear_target();
        for (const auto *verts:{&a,&b}) {
            GlesBatch raw{};raw.draw=d;raw.clip_coordinates=true;raw.clip_viewport=viewport;
            raw.vertices.assign(verts->begin(),verts->end());s.batches.push_back(std::move(raw));
        }
        assert(ge_gpu_backend_finish_color_frame(400+test));auto reference=read_all();
        clear_target();
        assert(ge_gpu_backend_accumulate_clip_vertices(d,viewport,a));
        assert(ge_gpu_backend_accumulate_clip_vertices(d,viewport,b));
        assert(s.batches.size()==1 && !s.batches[0].indices.empty());
        assert(ge_gpu_backend_finish_color_frame(450+test));auto actual=read_all();
        assert(reference==actual);++merged_images;
    }
    std::cout<<"PASS: "<<merged_images<<" actual merged list/strip/fan images equal native GL primitive reference, flat colour, winding, blend overlap and boundaries\n";

    // A 2x target must really allocate four times as many pixels, while logical
    // coordinates/UV and uploaded texture contents remain unchanged.
    s.scale=2;
    auto doubled=color;
    auto left=quad(0,0,8,16),right=quad(8,0,16,16);
    for(auto &v:left)v.rgba=0xff0000ff;
    for(auto &v:right)v.rgba=0xffff0000;
    ge_gpu_backend_accumulate_color_triangles(doubled,left);
    ge_gpu_backend_accumulate_color_triangles(doubled,right);
    assert(ge_gpu_backend_finish_color_frame(490));
    const auto &rt=s.targets.at(0x10000u);
    assert(rt.render_width==32 && rt.render_height==32);
    assert((pixel(7,16)==std::array<unsigned char,4>{255,0,0,255}));
    assert((pixel(24,16)==std::array<unsigned char,4>{0,0,255,255}));
    // Flip back without retaining a stale size/sampler or invalid FBO.
    s.scale=1;
    ge_gpu_backend_accumulate_color_triangles(doubled,left);
    ge_gpu_backend_accumulate_color_triangles(doubled,right);
    assert(ge_gpu_backend_finish_color_frame(491));
    assert(rt.render_width==16 && rt.render_height==16);
    assert((pixel(3,8)==std::array<unsigned char,4>{255,0,0,255}));
    assert((pixel(12,8)==std::array<unsigned char,4>{0,0,255,255}));
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS: actual 1x -> 2x -> 1x FBO sizes and identical logical scene; no GL errors\n";

    // The old backend silently disabled every unrecognized blend pair. Compare
    // every supported PSP factor/equation against the ACTUAL software blender.
    unsigned blend_cases=0;
    vp.flat_shading=false;
    const std::array<std::uint32_t,3> sources{0x4070dc28u,0xc87c25d9u,0xff20ff10u};
    const std::uint32_t destination=0x70523b91u;
    for(unsigned eq=0;eq<6;eq++) for(unsigned fa=0;fa<=10;fa++)
    for(unsigned fb=0;fb<=10;fb++) for(auto source:sources) {
        auto b=color;
        b.blend_enabled=true;b.blend_equation=eq;b.blend_source_factor=fa;b.blend_dest_factor=fb;
        b.blend_fix_source=0x2963c9;b.blend_fix_dest=0xcf2591;
        b.color_write_mask=(blend_cases%3u)==0?0x00F0330Fu:0u;
        clear_target();
        glClearColor((destination&255)/255.f,((destination>>8)&255)/255.f,
                     ((destination>>16)&255)/255.f,((destination>>24)&255)/255.f);
        glClear(GL_COLOR_BUFFER_BIT);
        auto v=quad(0,0,16,16);for(auto &x:v)x.rgba=source;
        ge_gpu_backend_accumulate_color_triangles(b,v);
        assert(ge_gpu_backend_finish_color_frame(500+blend_cases));
        auto actual=pixel(5,8);
        auto expected=lcs_reference_blend(source,destination,eq,fa,fb,b.blend_fix_source,b.blend_fix_dest,b.color_write_mask);
        for(unsigned channel=0;channel<4;channel++) {
            const int want=(expected>>(8u*channel))&255u;
            if(std::abs(int(actual[channel])-want)>1) {
                std::cerr<<"blend mismatch eq="<<eq<<" fa="<<fa<<" fb="<<fb<<" channel="<<channel<<" expected="<<want<<" actual="<<int(actual[channel])<<" mask="<<std::hex<<b.color_write_mask<<std::dec<<'\n';
                return 1;
            }
        }
        ++blend_cases;
    }
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS: "<<blend_cases<<" actual GLES blend images vs software reference, including independent FIXA/FIXB, double factors, subtract/min/max/absdiff and partial masks\n";
    // Within one batch, overlapped programmable blends must see previous triangles.
    auto b=color;b.blend_enabled=true;b.blend_equation=5;b.blend_source_factor=10;b.blend_dest_factor=10;
    auto v=quad(0,0,16,16);for(auto &x:v)x.rgba=0xff22cc66;
    clear_target();
    ge_gpu_backend_accumulate_color_triangles(b,v);
    ge_gpu_backend_accumulate_color_triangles(b,v);
    assert(ge_gpu_backend_finish_color_frame(9999));
    assert(pixel(5,8)[0]==0 && pixel(5,8)[1]==0 && pixel(5,8)[2]==0);
    std::cout<<"PASS: ordered overlapping programmable-blend triangles, coherentFetch="<<s.framebuffer_fetch_enabled<<"\n";

    // Fixed PSP LOD must select the requested mip even for a magnified quad.
    auto mip=color;mip.texture_enabled=true;mip.texture_format=3;
    mip.texture_function=3;mip.texture_use_alpha=true;mip.texture_address=0x088f0000;
    mip.texture_width=mip.texture_height=mip.texture_buffer_width=2;
    mip.texture_max_level=1;mip.texture_mipmap_enabled=true;mip.texture_clamp_u=mip.texture_clamp_v=true;
    mip.texture_level_addresses[0]=mip.texture_address;mip.texture_level_addresses[1]=mip.texture_address+16;
    mip.texture_level_widths[0]=mip.texture_level_heights[0]=mip.texture_level_buffer_widths[0]=2;
    mip.texture_level_widths[1]=mip.texture_level_heights[1]=mip.texture_level_buffer_widths[1]=1;
    mip.texture_content_signature=998877;
    ge_gpu_backend_prepare_texture_keys(mip);ge_gpu_backend_record_draw(mip);
    std::vector<std::byte> levels;
    for(unsigned n=0;n<4;n++)for(auto byte:red)levels.push_back(byte);
    for(auto byte:blue)levels.push_back(byte);
    assert(ge_gpu_backend_upload_decoded_texture_chain_packed(mip,2,2,2,levels));
    auto mip1=mip;mip1.texture_level_mode=1;mip1.texture_level_offset16=16;
    ge_gpu_backend_prepare_texture_keys(mip1);
    assert(texture_key(mip1)==texture_key(mip)); // same image, distinct sampling
    const auto images=s.textures.size();assert(!ge_gpu_backend_texture_needed(mip1));
    clear_target();ge_gpu_backend_accumulate_color_triangles(mip1,quad(0,0,16,16,2,2));
    assert(ge_gpu_backend_finish_color_frame(10001));assert(pixel(8,8)[2]==255 && pixel(8,8)[0]==0);
    mip.texture_level_mode=1;mip.texture_level_offset16=0;
    clear_target();ge_gpu_backend_accumulate_color_triangles(mip,quad(0,0,8,16,2,2));
    ge_gpu_backend_accumulate_color_triangles(mip1,quad(8,0,16,16,2,2));
    assert(s.batches.size()==2); // LOD changes cannot be merged
    assert(ge_gpu_backend_finish_color_frame(10002));
    assert(pixel(3,8)[0]==255 && pixel(12,8)[2]==255);
    auto filtering=mip;filtering.texture_min_linear=true;filtering.texture_clamp_u=false;
    ge_gpu_backend_prepare_texture_keys(filtering);
    assert(texture_key(mip)==texture_key(filtering) && s.textures.size()==images);
    std::cout<<"PASS: actual fixed LOD red/blue mips, adjacent LOD barriers, shared image across sampler changes\n";
    // Identical geometry reuses three allocations, not endless orphan buffers.
    for(unsigned i=0;i<6;i++){
        ge_gpu_backend_accumulate_color_triangles(mip,quad(0,0,16,16,2,2));
        assert(ge_gpu_backend_finish_color_frame(11000+i));
    }
    const auto allocations=s.geometry_allocations;
    for(unsigned i=0;i<100;i++){
        ge_gpu_backend_accumulate_color_triangles(mip,quad(0,0,16,16,2,2));
        assert(ge_gpu_backend_finish_color_frame(12000+i));
        assert(pixel(8,8)[0]==255);
    }
    assert(s.geometry_allocations==allocations);
    for(const auto &slot:s.geometry_slots) assert(slot.vertex_capacity && slot.indices && slot.vertices);
    assert(sizeof(GlesStreamVertex)==36);
    std::cout<<"PASS: 100 real fenced triple-buffer reuses with identical readback; no extra geometry allocation; 36-byte float vertex\n";
    // Current-frame textures must survive budget pressure. Prune only old images.
    auto keep_budget=s.texture_cache_byte_limit;auto keep_entries=s.texture_cache_entry_limit;
    s.texture_cache_entry_limit=2;
    for(auto &[key,image]:s.textures)image.last_used_epoch=s.frame_epoch;
    auto pinned=s.textures.size();trim_texture_cache(s,true);assert(s.textures.size()==pinned);
    ++s.frame_epoch;trim_texture_cache(s,true);assert(s.textures.size()<=2);
    s.texture_cache_byte_limit=keep_budget;s.texture_cache_entry_limit=keep_entries;
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS: sorted budget eviction preserves pending-frame textures; no GL errors\n";
    destroy_backend(s);
}
