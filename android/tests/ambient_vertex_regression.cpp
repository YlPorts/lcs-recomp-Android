// Self-contained CI guard/fallback coverage; real captured-frame correctness
// and timings are tested separately by capture_vertex_replay.cpp.
#include "../../lcs/host/ge_renderer.cpp"
#include <cassert>
#include <iostream>
#include <random>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return 0; }
const char *runtime_thread_name() noexcept { return "ambient-vertex-test"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}
using namespace lcs;
static std::uint32_t f24(float value) { return std::bit_cast<std::uint32_t>(value)>>8u; }
static void same(const Vertex &a,const Vertex &b) {
    assert(std::bit_cast<std::uint32_t>(a.color)==std::bit_cast<std::uint32_t>(b.color));
    const float av[]{a.x,a.y,a.z,a.w,a.inv_w,a.u,a.v,a.q,a.fog_factor};
    const float bv[]{b.x,b.y,b.z,b.w,b.inv_w,b.u,b.v,b.q,b.fog_factor};
    for (unsigned i=0;i<std::size(av);++i)
        assert(std::bit_cast<std::uint32_t>(av[i])==std::bit_cast<std::uint32_t>(bv[i]));
}
int main() {
    std::mt19937 random(0x617);
    std::uniform_real_distribution<float> unit(-1.f,1.f);
    std::array<std::uint32_t,256> commands{};
    commands[0x17]=1;
    GeTransformState transform{}; reset_ge_transform_state(transform);
    VertexLayout layout{}; std::string error;
    assert(build_vertex_layout_cached(0x115,layout,error) && layout.stride==10);
    AmbientVertexColorCache colors;
    std::array<std::uint8_t,10> record{};
    unsigned cases=0;
    for (unsigned material=0;material<64;++material) {
        commands[0x53]=material&7u;
        commands[0x54]=random()&0xffffffu; commands[0x55]=random()&0xffffffu;
        commands[0x58]=random()&255u; commands[0x5c]=random()&0xffffffu;
        commands[0x5d]=random()&255u;
        commands[0xc0]=(material&1u)?3u:0u;
        commands[0x1f]=(material>>1u)&1u;
        commands[0xcd]=f24(unit(random)); commands[0xce]=f24(unit(random));
        commands[0x48]=f24(unit(random)); commands[0x49]=f24(unit(random));
        commands[0x4a]=f24(unit(random)); commands[0x4b]=f24(unit(random));
        commands[0xb8]=(material%10u)|(((material/10u)%10u)<<8u);
        for (auto &element:transform.world) element=unit(random);
        if (material%8==0) transform.world.fill(0.f); // original normal fallback
        for (auto &element:transform.view) element=unit(random);
        for (auto &element:transform.projection) element=unit(random);
        const auto lighting=prepare_lighting(true,commands);
        VertexLightingCache original_colors; original_colors.begin(lighting);
        colors.begin(lighting);
        assert(ambient_0115_eligible(layout,commands[0xc0],true,colors));
        std::array<std::uint16_t,8> packed_colors{};
        for (auto &color:packed_colors) color=random();
        for (unsigned vertex=0;vertex<32;++vertex) {
            for (auto &byte:record) byte=random();
            const auto packed=packed_colors[vertex%packed_colors.size()];
            record[layout.color_offset]=packed&255u;
            record[layout.color_offset+1]=packed>>8u;
            VertexByteView view{record.data(),record.size()};
            Vertex reference{},actual{};
            assert(decode_vertex_optimized(view,0,layout,commands,transform,
                reference,error,&lighting,&original_colors));
            assert(decode_vertex_0115_ambient_fast(view,0,layout,commands,transform,
                actual,error,lighting,colors));
            same(reference,actual); ++cases;
        }
    }
    const auto ambient=prepare_lighting(true,commands);
    colors.begin(ambient);
    assert(!ambient_0115_eligible(layout,0,false,colors));
    assert(!ambient_0115_eligible(layout,1,true,colors));
    assert(!ambient_0115_eligible(layout,2,true,colors));
    for (auto type:{0x800115u,0x1115u,0x1ffu,0x315u,0x40115u}) {
        VertexLayout other{}; assert(build_vertex_layout_cached(type,other,error));
        assert(!ambient_0115_eligible(other,0,true,colors));
    }
    for (unsigned light=0;light<4;++light) {
        auto active=ambient; active.lights[light].enabled=true;
        colors.begin(active); assert(!ambient_0115_eligible(layout,0,true,colors));
    }
    auto unlit=ambient; unlit.enabled=false;
    colors.begin(unlit); assert(!ambient_0115_eligible(layout,0,true,colors));
    colors.begin(ambient);
    Vertex invalid{}; VertexByteView truncated{record.data(),record.size()-1};
    assert(!decode_vertex_0115_ambient_fast(truncated,0,layout,commands,transform,
        invalid,error,ambient,colors));
    std::cout<<"PASS: "<<cases<<" ambient-only vertices bit-exact across material changes, "
        "colour cache hits/collisions, transforms, UV and fog; enabled lights, other "
        "layouts/modes, software path and malformed input retain guarded fallback\n";
}
