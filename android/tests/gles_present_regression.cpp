// Exercise the actual present pass after a culled PSP draw, reading the window
// surface as well as the offscreen target. A correct target alone is not enough.
#ifndef LCS_GLES_SOURCE
#define LCS_GLES_SOURCE "../../lcs/host/ge_gpu_backend_gles.cpp"
#endif
#include LCS_GLES_SOURCE
#include <cassert>
#include <iostream>
namespace lcs {
const LcsConfiguration &lcs_render_configuration() { static LcsConfiguration c; return c; }
void runtime_log_line(std::string_view) {}
void runtime_log_error(std::string_view, std::string_view text) { std::cerr << text << '\n'; }
namespace android_host {
void set_source_size(std::uint32_t, std::uint32_t) noexcept {}
float ultrawide_x_scale() noexcept { return 1; }
}
}
using namespace lcs;
int main() {
    using GetDisplay = EGLDisplay (*)(EGLenum, void *, const EGLint *);
    const auto get = reinterpret_cast<GetDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    assert(get);
    auto &s = state();
    s.display = get(0x31DD, nullptr, nullptr);
    assert(eglInitialize(s.display, nullptr, nullptr));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, 0x40,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    assert(eglChooseConfig(s.display, ca, &s.config, 1, &count) && count);
    const EGLint pa[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    s.surface = eglCreatePbufferSurface(s.display, s.config, pa);
    s.context = eglCreateContext(s.display, s.config, EGL_NO_CONTEXT, ctx);
    assert(s.surface != EGL_NO_SURFACE && s.context != EGL_NO_CONTEXT);
    s.surface_dirty.store(false); s.enabled = true; s.scale = 1;
    ge_gpu_backend_set_display_framebuffer(0x04010000, 16, 16);
    GeGpuDrawDescriptor d{};
    d.framebuffer_address = 0x04010000; d.framebuffer_stride = 16;
    d.framebuffer_format = 3; d.scissor_x1 = d.scissor_y1 = 15; d.primitive = 3;
    GeGpuClipViewport vp{};
    assert(build_ge_gpu_clip_viewport(8, -8, 32767, 8, 8, 32767, true, vp));
    vp.cull_enabled = true;
    auto pixel = [&](GLuint fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        std::array<std::uint8_t, 4> p{};
        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
        return p;
    };
    for (unsigned winding = 0; winding < 2; ++winding) {
        vp.accept_counter_clockwise = winding != 0;
        std::array<GeGpuVertex, 3> triangle{};
        triangle[0].x = -1; triangle[0].y = -1;
        triangle[1].x = 3; triangle[1].y = -1;
        triangle[2].x = -1; triangle[2].y = 3;
        if (winding) std::swap(triangle[0], triangle[1]);
        for (auto &v : triangle) v.rgba = 0xff46321eu;
        assert(ge_gpu_backend_accumulate_clip_vertices(d, vp, triangle));
        assert(ge_gpu_backend_finish_color_frame(winding + 1));
        const auto expected = std::array<std::uint8_t, 4>{30, 50, 70, 255};
        assert(pixel(s.targets.at(0x10000).fbo) == expected);
        if (pixel(0) != expected) {
            std::cerr << "FAIL: valid game target becomes black during presentation after winding="
                      << winding << '\n';
            return 12;
        }
    }
    assert(glGetError() == GL_NO_ERROR);
    std::cout << "PASS: actual GLES window pixels survive both preceding game-draw culling directions\n";
    destroy_backend(s);
}
