// Captured LCS 0.6.16 draws 1665..1669: 512x320 scene -> RGB565 64x64
// environment map at 0x04000000 -> 50 vehicle chrome draws. The scissor maxima
// are (512,320)/(64,64), while REGION2 maxima are (511,319)/(63,63).
#define main earlier_present_regression
#include "gles_present_regression.cpp"
#undef main

namespace {
constexpr std::uint32_t scene_address = 0x04088000u;
constexpr std::uint32_t env_address = 0x04000000u;
constexpr std::uint32_t output_address = 0x04180000u;
constexpr std::uint32_t reference_address = 0x041a0000u;
constexpr std::uint32_t red = 0xff0000ffu, green = 0xff00ff00u;
constexpr std::uint32_t blue = 0xffff0000u, white = 0xffffffffu;

GeGpuDrawDescriptor descriptor(std::uint32_t address, unsigned width,
                               unsigned height, unsigned format = 3) {
    GeGpuDrawDescriptor d{};
    d.framebuffer_address = address; d.framebuffer_stride = width;
    d.framebuffer_format = format; d.primitive = 3;
    d.scissor_x1 = width; d.scissor_y1 = height;
    d.region_defined = true; d.region_x1 = width - 1; d.region_y1 = height - 1;
    return d;
}
std::array<GeGpuVertex, 6> rectangle(float x0, float y0, float x1, float y1,
    std::uint32_t color, float u0=0, float v0=0, float u1=0, float v1=0) {
    std::array<GeGpuVertex, 6> out{};
    const float data[6][4] = {{x0,y0,u0,v0},{x1,y0,u1,v0},{x1,y1,u1,v1},
                              {x0,y0,u0,v0},{x1,y1,u1,v1},{x0,y1,u0,v1}};
    for (unsigned i=0; i<6; ++i) {
        out[i].x=data[i][0]; out[i].y=data[i][1]; out[i].rgba=color;
        out[i].u=data[i][2]; out[i].v=data[i][3];
    }
    return out;
}
void queue(const GeGpuDrawDescriptor &d, std::span<const GeGpuVertex> v) {
    ge_gpu_backend_record_draw(d);
    ge_gpu_backend_accumulate_color_triangles(d, v);
}
void finish() { static std::uint64_t frame=0; assert(ge_gpu_backend_finish_color_frame(++frame)); }
std::array<std::uint8_t,4> read_pixel(std::uint32_t address, unsigned x, unsigned y) {
    const auto &s=state(); const auto &t=s.targets.at(address & 0x001ffff0u);
    glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
    std::array<std::uint8_t,4> p{};
    glReadPixels(x*s.scale+s.scale/2,t.render_height-1-y*s.scale-s.scale/2,
                 1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());
    return p;
}
std::vector<std::byte> map_pixels() {
    const auto &s=state(); const auto &t=s.targets.at(0u);
    const auto size=64u*s.scale;
    glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
    std::vector<std::byte> bottom(size*size*4u), top(bottom.size());
    glReadPixels(0,t.render_height-size,size,size,GL_RGBA,GL_UNSIGNED_BYTE,bottom.data());
    for (unsigned y=0; y<size; ++y)
        std::memcpy(top.data()+y*size*4u,bottom.data()+(size-1-y)*size*4u,size*4u);
    return top;
}
void configure_texture(GeGpuDrawDescriptor &d,std::uint32_t address,
                       unsigned width,unsigned height,unsigned stride,unsigned format) {
    d.texture_enabled=true; d.texture_address=address;
    d.texture_width=width; d.texture_height=height; d.texture_buffer_width=stride;
    d.texture_format=format; d.texture_function=3; d.texture_use_alpha=true;
}
}

int main() {
    using GetDisplay=EGLDisplay(*)(EGLenum,void*,const EGLint*);
    auto get=reinterpret_cast<GetDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT")); assert(get);
    auto &s=state(); s.display=get(0x31DD,nullptr,nullptr);
    assert(eglInitialize(s.display,nullptr,nullptr)); assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint config[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLint n=0; assert(eglChooseConfig(s.display,config,&s.config,1,&n) && n);
    const EGLint surface[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    const EGLint context[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    s.surface=eglCreatePbufferSurface(s.display,s.config,surface);
    s.context=eglCreateContext(s.display,s.config,EGL_NO_CONTEXT,context);
    assert(s.surface!=EGL_NO_SURFACE && s.context!=EGL_NO_CONTEXT);
    s.surface_dirty.store(false); s.enabled=true;
    unsigned edge_cases=0;
    for (unsigned scale : {1u,2u}) {
        s.scale=scale;
        ge_gpu_backend_set_display_framebuffer(scene_address,512,320);
        auto scene=descriptor(scene_address,512,320);
        auto stale=descriptor(env_address,512,320);
        // Reused-address case: stale bright green outside the new valid map
        // must never become a vehicle reflection, including its filter edges.
        queue(stale,rectangle(0,0,512,320,green));
        queue(scene,rectangle(0,0,512,160,red));
        queue(scene,rectangle(256,0,512,160,blue));
        queue(scene,rectangle(0,160,512,320,0xff000000u));
        queue(scene,rectangle(256,160,512,320,white));
        auto env=descriptor(env_address,64,64,0);
        configure_texture(env,scene_address,512,512,512,3);
        env.texture_clamp_u=env.texture_clamp_v=true;
        // Exact capture source rectangle, including its nonzero U origin.
        queue(env,rectangle(0,0,64,64,white,85,0,426,320));
        auto chrome=descriptor(output_address,64,64);
        configure_texture(chrome,env_address,64,64,64,0);
        chrome.texture_min_linear=chrome.texture_mag_linear=true;
        const auto refreshes=s.report.vram_feedback_refreshes;
        for (unsigned i=0; i<50; ++i) {
            // Keep consumers as separate GPU batches while retaining the same
            // sampling behavior; ENV is ignored by texture REPLACE.
            chrome.texture_env=i;
            queue(chrome,rectangle(0,0,64,64,white,0,0,64,64));
        }
        finish();
        if (read_pixel(output_address,16,16)!=std::array<std::uint8_t,4>{255,0,0,255}) {
            const auto p=read_pixel(output_address,16,16);
            std::cerr << "FAIL: vehicle samples stale framebuffer area instead of the 64x64 reflection: "
                      << unsigned(p[0]) << ',' << unsigned(p[1]) << ',' << unsigned(p[2]) << '\n';
            return 20;
        }
        assert((read_pixel(output_address,48,16)==std::array<std::uint8_t,4>{0,0,255,255}));
        assert((read_pixel(output_address,48,48)==std::array<std::uint8_t,4>{255,255,255,255}));
        assert((read_pixel(scene_address,511,319)==std::array<std::uint8_t,4>{255,255,255,255}));
#ifndef LCS_BASELINE_TEST
        assert(s.report.vram_feedback_refreshes==refreshes+1);
        assert(s.targets.at(0u).logical_width==512 && s.targets.at(0u).logical_height==320);
        assert(s.targets.at(0u).framebuffer_stride==64 && s.targets.at(0u).framebuffer_format==0);
        assert(s.targets.at(0u).sampling_width==64*scale && s.targets.at(0u).sampling_height==64*scale);
        assert(s.targets.at(scene_address & 0x001ffff0u).logical_width==512);
        assert(s.targets.at(scene_address & 0x001ffff0u).logical_height==320);
#endif
        // Compare actual feedback with an independently uploaded, tightly
        // packed image of the valid map. This validates bilinear wrap/clamp
        // around every edge, including the UV values in the user's capture.
        auto reference=chrome; reference.framebuffer_address=reference_address;
        reference.texture_address=0x08900000u+scale*0x10000u;
        reference.texture_content_signature=1000u+scale;
        const auto pixels=map_pixels();
        ge_gpu_backend_prepare_texture_keys(reference); ge_gpu_backend_record_draw(reference);
        assert(ge_gpu_backend_upload_decoded_texture(reference,64*scale,64*scale,pixels));
        for (bool clamp : {false,true}) {
            chrome.texture_clamp_u=chrome.texture_clamp_v=clamp;
            reference.texture_clamp_u=reference.texture_clamp_v=clamp;
            for (const auto &uv : {std::array<float,2>{0.0178223f,16}, {63.9741f,16},
                                  {16,0.251947f}, {16,63.8734f}, {-0.25f,16}, {64.25f,16}}) {
                const auto v=rectangle(0,0,16,16,white,uv[0],uv[1],uv[0],uv[1]);
                queue(chrome,v); queue(reference,v); finish();
                if(read_pixel(output_address,8,8)!=read_pixel(reference_address,8,8)) {
                    std::cerr << "FAIL: reflection edge differs from tight texture, scale=" << scale
                              << " clamp=" << clamp << " uv=" << uv[0] << ',' << uv[1] << '\n';
                    return 21;
                }
                ++edge_cases;
            }
        }
#ifndef LCS_BASELINE_TEST
        const auto revision=s.targets.at(0u).sampling_revision;
        auto overwrite=descriptor(env_address,64,64,0);
        queue(overwrite,rectangle(0,0,64,64,blue));
        queue(chrome,rectangle(0,0,64,64,white,0,0,64,64)); finish();
        assert(s.targets.at(0u).sampling_revision>revision);
        assert((read_pixel(output_address,8,8)==std::array<std::uint8_t,4>{0,0,255,255}));
        // Self-feedback must snapshot before the draw, then invalidate the view.
        auto self=overwrite; configure_texture(self,env_address,64,64,64,0);
        self.texture_function=0;
        queue(self,rectangle(0,0,64,64,0xff808080u,0,0,64,64));
        queue(chrome,rectangle(0,0,64,64,white,0,0,64,64)); finish();
        const auto dark_blue=read_pixel(output_address,8,8);
        assert(dark_blue[0]==0 && dark_blue[1]==0 && dark_blue[2]>=123 && dark_blue[2]<=132);
        // A new reflection target stays 64x64 instead of inheriting the display.
        auto fresh=overwrite; fresh.framebuffer_address=0x04120000u+scale*0x1000u;
        fresh.region_x0=37; fresh.region_y0=29; // REGION1 is not a raster origin.
        queue(fresh,rectangle(0,0,65,65,red)); finish();
        const auto &small=s.targets.at(fresh.framebuffer_address & 0x001ffff0u);
        assert(small.logical_width==64 && small.logical_height==64);
        assert((read_pixel(fresh.framebuffer_address,0,0)==std::array<std::uint8_t,4>{255,0,0,255}));
        // Queuing later draws does not rewrite the stride of already-rendered data.
        ge_gpu_backend_record_draw(stale);
        assert(s.targets.at(0u).framebuffer_stride==64);
        // A later queued enlargement must not change sampling of an existing
        // smaller GL allocation or permit an out-of-bounds copy.
        auto future=descriptor(fresh.framebuffer_address,512,320,0);
        ge_gpu_backend_record_draw(future);
        auto early=chrome;
        configure_texture(early,fresh.framebuffer_address,32,32,64,0);
        queue(early,rectangle(0,0,64,64,white,0,0,32,32)); finish();
        assert(small.logical_width==512 && small.allocation_width==64);
        assert(small.sampling_width==32*scale && small.sampling_height==32*scale);
        assert((read_pixel(output_address,8,8)==std::array<std::uint8_t,4>{255,0,0,255}));
        early.texture_width=early.texture_height=128;
        assert(!feedback_needs_view(small,early));
        assert(texture_for_draw(s,early,nullptr)==small.color);
#endif
        assert(glGetError()==GL_NO_ERROR);
    }
#ifndef LCS_BASELINE_TEST
    // Exercise the actual GL cache beyond its global budget, including targets
    // whose retained views are evicted while their framebuffer remains alive.
    std::string error;
    for (unsigned i=0; i<5; ++i) {
        const auto address=0x04001000u+i*16u;
        auto &t=target_metadata(s,address);
        t.logical_width=t.logical_height=1024;
        assert(ensure_target(s,t,error));
        t.framebuffer_stride=1024; t.framebuffer_format=3;
        auto d=descriptor(output_address,64,64);
        configure_texture(d,address,1024,1023,1024,3);
        assert(texture_for_draw(s,d,nullptr)==t.sampling_view);
        assert(s.sampling_view_bytes<=kSamplingViewByteLimit);
    }
    unsigned large_retained=0;
    std::uint64_t view_bytes=0;
    for (const auto &[address,t]:s.targets) {
        if(t.sampling_view) {
            if(t.sampling_width==2048 && t.sampling_height==2046) ++large_retained;
            view_bytes+=std::uint64_t(t.sampling_width)*t.sampling_height*4u;
        }
    }
    assert(large_retained<5 && view_bytes==s.sampling_view_bytes);
    assert(glGetError()==GL_NO_ERROR);
#endif
    std::cout << "PASS: captured 512x320 -> 64x64 RGB565 -> 50 vehicle draws, reused green backing, "
              << edge_cases << " repeat/clamp edge comparisons at 1x/2x, source invalidation, self-feedback, "
              << "REGION2 bounds, queued allocation growth and bounded view cache\n";
    destroy_backend(s);
#ifndef LCS_BASELINE_TEST
    assert(s.sampling_view_bytes==0);
#endif
}
