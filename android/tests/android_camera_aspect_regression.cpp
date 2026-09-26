// Exercise the actual guest aspect/extent hooks, CPU vertex reader, BBOX and
// early-rejection code with the uploaded scene's projection/viewport geometry.
#ifndef LCS_TEST_RENDERER
#define LCS_TEST_RENDERER "../../lcs/host/ge_renderer.cpp"
#endif
#ifndef LCS_TEST_CONFIG
#define LCS_TEST_CONFIG "../../lcs/host/lcs_render_config.cpp"
#endif
#include LCS_TEST_RENDERER
#include LCS_TEST_CONFIG
#include <cassert>
#include <iostream>

namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return 0; }
const char *runtime_thread_name() noexcept { return "android-camera-aspect-test"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}
namespace lcs::android_host {
static std::uint32_t test_width=1536,test_height=709,test_source_width=480,test_source_height=272;
std::uint32_t display_width() noexcept { return test_width; }
std::uint32_t display_height() noexcept { return test_height; }
std::uint32_t source_width() noexcept { return test_source_width; }
std::uint32_t source_height() noexcept { return test_source_height; }
float ultrawide_x_scale() noexcept {
    if (!test_width || !test_height || !test_source_width || !test_source_height) return 1.f;
    return std::clamp((float(test_source_width)/test_source_height)/
        (float(test_width)/test_height),.5f,1.5f);
}
#ifdef LCS_BASELINE_TEST
float geometry_x_scale() noexcept { return ultrawide_x_scale(); }
#endif
}
using namespace lcs;
static bool close(float a,float b) { return std::abs(a-b)<0.0002f; }
int main() {
    auto &config=global_configuration();
    config.initialized=true; config.widescreen.enabled=true;
    config.rendering.internal_resolution_mode=InternalResolutionMode::Scale;
    config.rendering.internal_scale=2;
    constexpr float native_aspect=16.f/9.f,display_aspect=1536.f/709.f;
    const float stretch=lcs_widescreen_extent(1.f);
    if (!close(lcs_widescreen_aspect(native_aspect),display_aspect)) {
        std::cerr<<"FAIL: guest camera remains at internal-render aspect "
            <<lcs_widescreen_aspect(native_aspect)<<" while physical display is "<<display_aspect<<'\n';
        return 20;
    }
    assert(close(stretch,display_aspect/native_aspect));
    assert(android_host::geometry_x_scale()==1.f);
    const float hud_scale=android_host::ultrawide_x_scale();
    assert(hud_scale<1.f); // HUD correction remains independent.
    config.rendering.internal_scale=1;
    assert(lcs_widescreen_extent(1.f)==stretch);
    config.rendering.internal_resolution_mode=InternalResolutionMode::Custom;
    config.rendering.internal_width=1024; config.rendering.internal_height=640;
    assert(lcs_widescreen_extent(1.f)==stretch);
    android_host::test_source_width=64; android_host::test_source_height=64;
    assert(lcs_widescreen_extent(1.f)==stretch);
    android_host::test_source_width=480; android_host::test_source_height=272;

    psprecomp::GuestMemory memory;
    constexpr std::uint32_t address=0x08010000u;
    std::array<std::uint32_t,256> commands{};
    commands[0x12]=0x1ffu; commands[0x1c]=1;
    VertexLayout layout{}; std::string error;
    assert(build_vertex_layout_cached(commands[0x12],layout,error));
    GeTransformState transform{}; reset_ge_transform_state(transform);
    transform.projection.fill(0.f);
    constexpr float captured_x=1.428131103515625f,captured_y=2.53887939453125f;
    transform.projection[5]=captured_y;
    transform.projection[10]=-1.000885009765625f;
    transform.projection[11]=-1.f; transform.projection[14]=-.2000885f;
    GeGpuClipViewport viewport{};
    assert(build_ge_gpu_clip_viewport(256.f,-160.f,32767.5f,256.f,160.f,32767.5f,true,viewport));
    assert(viewport.x==0.f && viewport.y==0.f && viewport.width==512.f && viewport.height==320.f);
    unsigned sides=0;
    for (float sign:{-1.f,1.f}) {
        for (unsigned vertex=0;vertex<3;++vertex) {
            const float xyz[]{sign*(1.06f+vertex*.02f)/captured_x,
                              (float(vertex)-1.f)*.05f,-1.f};
            for (unsigned axis=0;axis<3;++axis)
                memory.store32(address+vertex*layout.stride+layout.position_offset+axis*4,
                    std::bit_cast<std::uint32_t>(xyz[axis]));
        }
        // Previously the guest used this narrow projection even though a later
        // GE-only correction made the same triangle visible at the screen edge.
        transform.projection[0]=captured_x;
        assert(ge_position_only_outside(memory,commands,transform,layout,address,0,3,1.f));
        assert(!ge_position_only_outside(memory,commands,transform,layout,address,0,3,hud_scale));
        // Existing guest extent hooks widen the projection BEFORE visibility.
        transform.projection[0]=captured_x/lcs_widescreen_extent(1.f);
        assert(!ge_position_only_outside(memory,commands,transform,layout,address,0,3,
            android_host::geometry_x_scale()));
        GeBoundingBoxResult bounds{};
        assert(test_ge_bounding_box(memory,commands,transform,address,0,3,bounds,error));
        assert(bounds.visible && bounds.next_vertex_address==address+3*layout.stride);
        for (unsigned vertex=0;vertex<3;++vertex) {
            Vertex decoded{};
            assert(decode_vertex(memory,address+vertex*layout.stride,layout,commands,
                transform,decoded,error));
            decoded.x*=android_host::geometry_x_scale();
            assert(std::abs(decoded.x)<decoded.w);
            const float screen_x=256.f+256.f*decoded.x/decoded.w;
            assert(screen_x>=0.f && screen_x<512.f);
            for (float scale:{1.f,2.f}) assert(screen_x*scale<512.f*scale);
        }
        ++sides;
    }
    android_host::test_width=1920; android_host::test_height=1080;
    assert(lcs_widescreen_extent(1.f)==1.f);
    assert(lcs_widescreen_aspect(native_aspect)==native_aspect);
    assert(android_host::geometry_x_scale()==1.f);
    android_host::test_width=0; android_host::test_height=0;
    assert(lcs_widescreen_extent(1.f)==1.f);
    android_host::test_width=709; android_host::test_height=1536;
    assert(lcs_widescreen_extent(1.f)==1.f);
    config.widescreen.aspect_x=21; config.widescreen.aspect_y=9;
    assert(close(lcs_widescreen_aspect(native_aspect),21.f/9.f));
    config.widescreen.enabled=false;
    assert(android_host::geometry_x_scale()==android_host::ultrawide_x_scale());
    config.widescreen.enabled=true; config.initialized=false;
    assert(lcs_widescreen_extent(1.f)==1.f);
    assert(android_host::geometry_x_scale()==android_host::ultrawide_x_scale());
    std::cout<<"PASS: physical 1536x709 guest aspect/extent, "<<sides
        <<" widened sides retained by actual early/BBOX/vertex paths and 512x320 viewport; "
        "no duplicate 3D scale; HUD independent; internal/offscreen sizes ignored; "
        "16:9, missing/portrait size, explicit aspect and disabled-hook fallbacks\n";
}
