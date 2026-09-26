// Exercise the actual CPU vertex decoder with per-primitive environment vectors.
// Synthetic host timings below are not Android FPS measurements.
#include "../../lcs/host/ge_renderer.cpp"
#include <cassert>
#include <iostream>
#include <random>

namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return 0; }
const char *runtime_thread_name() noexcept { return "test"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}
using namespace lcs;

static std::uint32_t f24(float value) {
    return std::bit_cast<std::uint32_t>(value) >> 8u;
}
static void same(const Vertex &a, const Vertex &b) {
    assert(std::bit_cast<std::uint32_t>(a.color) == std::bit_cast<std::uint32_t>(b.color));
    const float av[]{a.x,a.y,a.z,a.w,a.inv_w,a.u,a.v,a.q,a.fog_factor};
    const float bv[]{b.x,b.y,b.z,b.w,b.inv_w,b.u,b.v,b.q,b.fog_factor};
    for (unsigned i=0; i<std::size(av); ++i)
        assert(std::bit_cast<std::uint32_t>(av[i]) == std::bit_cast<std::uint32_t>(bv[i]));
}

// Both timings execute this same loop and complete decoder. Only the optional
// invariant-vector preparation differs; unique normals avoid vertex memo hits.
[[gnu::noinline]] static std::pair<double,std::uint64_t> run_decode(
    const std::uint8_t *records, const VertexLayout &layout,
    const std::array<std::uint32_t,256> &cmd, const GeTransformState &tr,
    bool prepared) {
    constexpr unsigned count = 500000;
    const auto light = prepare_lighting(true,cmd);
    std::string error;
    const auto begin = std::chrono::steady_clock::now();
    const auto environment = prepare_environment_map(cmd);
    std::uint64_t checksum=0;
    for (unsigned n=0; n<count; ++n) {
        const VertexByteView view{records+(n%1024)*layout.stride,layout.stride};
        Vertex vertex{};
        const bool ok=decode_vertex_optimized(view,0,layout,cmd,tr,vertex,error,
            &light,nullptr,prepared ? &environment : nullptr);
        assert(ok);
        checksum+=std::bit_cast<std::uint32_t>(vertex.u);
        checksum+=std::bit_cast<std::uint32_t>(vertex.v);
        checksum+=std::bit_cast<std::uint32_t>(vertex.color);
    }
    return {std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-begin).count(),checksum};
}

int main() {
    psprecomp::GuestMemory memory;
    std::array<std::uint32_t,256> cmd{};
    GeTransformState tr{}; reset_ge_transform_state(tr);
    cmd[0x18]=1; cmd[0x5f]=1; cmd[0x5b]=f24(3.5f);
    cmd[0x55]=0x778899; cmd[0x56]=0xc0d0e0; cmd[0x57]=0x304050;
    cmd[0x58]=255; cmd[0x5c]=0x405060; cmd[0x5d]=255;
    cmd[0x8f]=0x808080; cmd[0x90]=0xffffff; cmd[0x91]=0x202020;
    cmd[0x48]=cmd[0x49]=f24(1); cmd[0xcd]=f24(1.2f); cmd[0xce]=f24(.5f);
    constexpr std::uint32_t base=0x08002000;
    const std::uint32_t types[]{0x115,0x1ff,0x19f,0x103,0x81ff,
        0x1ff|(1u<<18),0x1ff|(3u<<9),0x80019f};
    std::mt19937 rng(0x615e);
    std::uniform_real_distribution<float> unit(-1.f,1.f);
    unsigned comparisons=0;
    for (const auto type:types) {
        VertexLayout layout{}; std::string error;
        assert(build_vertex_layout_cached(type,layout,error));
        for (unsigned primitive=0; primitive<128; ++primitive) {
            cmd[0xc0]=primitive<96 ? 2 : primitive%4;
            cmd[0xc1]=(primitive%4)|(((primitive/4)%4)<<8);
            cmd[0x17]=primitive&1; cmd[0x53]=primitive&7;
            cmd[0x1f]=(primitive/2)&1; cmd[0x51]=(primitive/4)&1;
            cmd[0xb8]=(primitive%10)|(((primitive/10)%10)<<8);
            for (unsigned light=0; light<4; ++light) {
                for (unsigned axis=0; axis<3; ++axis) {
                    float value=unit(rng)*10.f;
                    if (primitive%17==0) value=0;
                    if (primitive%19==0) value=1.e-20f;
                    if (primitive%23==0) value=std::numeric_limits<float>::infinity();
                    if (primitive%29==0) value=std::numeric_limits<float>::quiet_NaN();
                    cmd[0x63+3*light+axis]=f24(value);
                }
            }
            // Changing selection and light values at every primitive must be
            // reflected immediately, including zero/non-finite fallbacks.
            const auto environment=prepare_environment_map(cmd);
            const auto lighting=prepare_lighting(layout.color_type>=4,cmd);
            for (unsigned vertex=0; vertex<12; ++vertex) {
                for (unsigned i=0; i<layout.stride; ++i) memory.store8(base+i,rng()&255);
                for (unsigned morph=0; morph<layout.morph_count; ++morph) {
                    const auto at=base+morph*layout.one_size;
                    for (const auto item:{std::pair{layout.tc_type,layout.tc_offset},
                        std::pair{layout.normal_type,layout.normal_offset},
                        std::pair{layout.position_type,layout.position_offset},
                        std::pair{layout.weight_type,layout.weight_offset}}) {
                        if (item.first==3u) {
                            const unsigned fields=item.second==layout.tc_offset ? 2u:3u;
                            for (unsigned axis=0; axis<fields && item.second+axis*4+4<=layout.one_size; ++axis)
                                memory.store32(at+item.second+axis*4,std::bit_cast<std::uint32_t>(unit(rng)));
                        }
                    }
                }
                VertexByteView view{memory.raw_pointer(base,layout.stride),layout.stride};
                Vertex reference{},actual{};
                assert(decode_vertex_optimized(view,0,layout,cmd,tr,reference,error,&lighting));
                assert(decode_vertex_optimized(view,0,layout,cmd,tr,actual,error,
                    &lighting,nullptr,&environment));
                same(reference,actual); ++comparisons;
            }
        }
    }
    std::cout<<"PASS: "<<comparisons<<" bit-exact environment-map decoder comparisons; "
        "all light selections, changing primitives, zero/non-finite vectors, skin/morph, "
        "lighting, fog, 2D and other UV modes\n";

    VertexLayout layout{}; std::string error;
    assert(build_vertex_layout_cached(0x1ff,layout,error));
    cmd[0xc0]=2; cmd[0xc1]=0x100; cmd[0x17]=0; cmd[0x51]=0;
    cmd[0x63]=f24(.7f); cmd[0x64]=f24(.2f); cmd[0x65]=f24(.6f);
    cmd[0x66]=f24(-.2f); cmd[0x67]=f24(.9f); cmd[0x68]=f24(.4f);
    for (unsigned n=0; n<1024; ++n) {
        const auto at=base+n*layout.stride;
        memory.store32(at+layout.color_offset,rng());
        for (unsigned axis=0; axis<3; ++axis) {
            memory.store32(at+layout.normal_offset+axis*4,std::bit_cast<std::uint32_t>(unit(rng)));
            memory.store32(at+layout.position_offset+axis*4,std::bit_cast<std::uint32_t>(unit(rng)));
        }
        memory.store32(at+layout.tc_offset,0); memory.store32(at+layout.tc_offset+4,0);
    }
    const auto *records=memory.raw_pointer(base,1024*layout.stride);
    std::array<double,5> before{},after{};
    for (unsigned n=0; n<5; ++n) {
        const auto first=run_decode(records,layout,cmd,tr,n%2!=0);
        const auto second=run_decode(records,layout,cmd,tr,n%2==0);
        assert(first.second==second.second);
        before[n]=n%2 ? second.first:first.first;
        after[n]=n%2 ? first.first:second.first;
    }
    std::sort(before.begin(),before.end()); std::sort(after.begin(),after.end());
    std::cout<<"HOST ONLY: 500000 reflective vertices, 5 alternating trials, median "
        "unpreparedMs="<<before[2]<<" preparedMs="<<after[2]
        <<"; synthetic CPU decoder timing, NOT Android or game FPS\n";
}
