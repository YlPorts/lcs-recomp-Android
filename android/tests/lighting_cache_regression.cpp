// Exact cache vs current reference lighting, including point/spot bypass and
// consecutive material changes. This is not a physical-device FPS benchmark.
#include "../../lcs/host/ge_renderer.cpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept{return 0;}
const char *runtime_thread_name() noexcept{return "test";}
std::uint32_t runtime_dispatch_pc() noexcept{return 0;}
}
using namespace lcs;
int main() {
    std::mt19937 rng(611);
    std::uniform_real_distribution<float> unit(-1.f,1.f);
    auto fc=[&]{return FloatColor{std::abs(unit(rng)),std::abs(unit(rng)),std::abs(unit(rng)),std::abs(unit(rng))};};
    VertexLightingCache cache;
    constexpr unsigned iterations=6000;
    for (unsigned n=0;n<iterations;n++) {
        PreparedLighting p{};
        p.enabled=n%17!=0;p.material_update=n&7;
        p.material_alpha=std::abs(unit(rng));p.material_ambient=fc();p.material_diffuse=fc();
        p.material_specular=fc();p.global_ambient=fc();p.emissive=fc();p.specular_exponent=3.5f;
        for (auto &l:p.lights) {
            l.enabled=true;l.type=(n%3);l.computation=n&3;
            l.vector=normalized_or_001({unit(rng),unit(rng),unit(rng)});
            l.attenuation={1,.25f,.15f};l.spot_direction={0,0,1};l.cutoff=.1f;l.exponent=2;
            l.diffuse=fc();l.ambient=fc();l.specular=fc();
        }
        const Color color=std::bit_cast<Color>(static_cast<std::uint32_t>(rng()));
        const Vec3 normal{unit(rng),unit(rng),unit(rng)};
        cache.begin(p);
        for(unsigned repeat=0;repeat<8;repeat++) {
            const Vec3 position{unit(rng)*10,unit(rng)*10,unit(rng)*10};
            const auto a=cache.apply(color,position,normal,p);
            const auto b=apply_prepared_lighting(color,position,normal,p);
            assert(std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b));
        }
    }
    assert(cache.hits>1000);
    std::cout<<"PASS: 48000 exact cached/reference lighting comparisons; material boundaries, directional position independence, point/spot bypass, hits="<<cache.hits<<"\n";
    // Actual decoder integration preserves all other attributes as well.
    psprecomp::GuestMemory mem; std::array<std::uint32_t,256> cmd{};
    GeTransformState transform{};reset_ge_transform_state(transform);
    VertexLayout layout{};std::string error;assert(build_vertex_layout_cached(0x1ffu,layout,error));
    constexpr auto address=0x08001000u;
    cmd[0x17]=1;cmd[0x18]=1;cmd[0x65]=std::bit_cast<std::uint32_t>(1.f)>>8;
    cmd[0x58]=255;cmd[0x55]=0x778899;cmd[0x56]=0xffffff;cmd[0x5c]=0x303030;cmd[0x5d]=255;cmd[0x90]=0x8090a0;
    for(unsigned n=0;n<2000;n++) {
        mem.store32(address+layout.color_offset,rng());
        for(unsigned j=0;j<3;j++) {
            mem.store32(address+layout.normal_offset+4*j,std::bit_cast<std::uint32_t>(unit(rng)));
            mem.store32(address+layout.position_offset+4*j,std::bit_cast<std::uint32_t>(unit(rng)));
        }
        auto prepared=prepare_lighting(true,cmd); cache.begin(prepared);
        Vertex a{},b{};
        assert(decode_vertex_optimized(mem,address,layout,cmd,transform,a,error,&prepared));
        assert(decode_vertex_optimized(mem,address,layout,cmd,transform,b,error,&prepared,&cache));
        assert(a.x==b.x && a.y==b.y && a.z==b.z && a.w==b.w);
        assert(a.u==b.u && a.v==b.v && a.q==b.q && a.fog_factor==b.fog_factor);
        assert(std::bit_cast<std::uint32_t>(a.color)==std::bit_cast<std::uint32_t>(b.color));
    }
    std::cout<<"PASS: 2000 decoder integrations preserve UV, positions, fog and exact colour\n";
    // Informational host-only microbenchmark with recurring normals.
    PreparedLighting p{};p.enabled=true;p.material_update=7;p.material_alpha=1;
    p.global_ambient={.1,.1,.1,1};p.specular_exponent=4.5;
    for(auto &l:p.lights){l.enabled=true;l.vector={0,0,1};l.diffuse={.6,.6,.6,1};l.specular={.3,.3,.3,1};l.computation=1;}
    std::array<Vec3,32> normals{};for(auto &n:normals)n=normalized_or_001({unit(rng),unit(rng),unit(rng)});
    const Color color{77,150,210,255};std::uint64_t reference_sum=0,cached_sum=0;
    constexpr unsigned total=500000;
    auto t=std::chrono::steady_clock::now();
    for(unsigned i=0;i<total;i++) reference_sum+=std::bit_cast<std::uint32_t>(apply_prepared_lighting(color,{0,0,0},normals[i&31],p));
    const auto original=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    t=std::chrono::steady_clock::now();cache.begin(p);
    for(unsigned i=0;i<total;i++) cached_sum+=std::bit_cast<std::uint32_t>(cache.apply(color,{0,0,0},normals[i&31],p));
    const auto cached=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    assert(reference_sum==cached_sum);
    std::cout<<"HOST ONLY: recurring-normal lighting "<<total<<" evaluations referenceMs="<<original<<" cachedMs="<<cached<<" checksum="<<cached_sum<<"; not Android FPS\n";
}
