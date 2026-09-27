// Fixed-size memoization must preserve the original decoder's exact outputs,
// cache hit/miss decisions, mutation handling and state invalidation.
#include "../../lcs/host/ge_renderer.cpp"
#include <cassert>
#include <iostream>
#include <random>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return 0; }
const char *runtime_thread_name() noexcept { return "vertex-memo-0115-test"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}
using namespace lcs;
static void same(const Vertex &a,const Vertex &b) {
    assert(std::bit_cast<std::uint32_t>(a.color)==std::bit_cast<std::uint32_t>(b.color));
    const float av[]{a.x,a.y,a.z,a.w,a.inv_w,a.u,a.v,a.q,a.fog_factor};
    const float bv[]{b.x,b.y,b.z,b.w,b.inv_w,b.u,b.v,b.q,b.fog_factor};
    for (unsigned i=0;i<std::size(av);++i)
        assert(std::bit_cast<std::uint32_t>(av[i])==std::bit_cast<std::uint32_t>(bv[i]));
}
int main() {
    std::mt19937 random(0x619);
    std::uniform_real_distribution<float> unit(-1.f,1.f);
    std::array<std::uint32_t,256> commands{};
    commands[0x17]=1u;
    commands[0x48]=commands[0x49]=std::bit_cast<std::uint32_t>(1.f)>>8;
    commands[0xb8]=0x808;
    VertexLayout layout{}; std::string error;
    assert(build_vertex_layout_cached(0x115,layout,error) && layout.stride==10u);
    GeTransformState transform{};reset_ge_transform_state(transform);
    GeVertexMemo<Vertex> generic,specialized;
    AmbientVertexColorCache colors;
    std::array<std::array<std::uint8_t,10>,384> records{};
    for (auto &record:records) for (auto &byte:record) byte=random();
    unsigned cases=0,hits=0,misses=0;
    for (unsigned state=1;state<=24;++state) {
        for (auto &value:transform.world) value=unit(random);
        for (auto &value:transform.view) value=unit(random);
        for (auto &value:transform.projection) value=unit(random);
        commands[0x55]=random()&0xffffffu;
        commands[0x58]=random()&255u;
        commands[0x5c]=random()&0xffffffu;
        commands[0x5d]=random()&255u;
        const auto lighting=prepare_lighting(true,commands);
        colors.begin(lighting);
        assert(!generic.begin_state(state,layout.type));
        assert(!specialized.begin_state(state,layout.type));
        assert(generic.begin_state(state,layout.type));
        assert(specialized.begin_state(state,layout.type));
        for (unsigned i=0;i<1536;++i) {
            // Small reused sets plus sets larger than256 exercise collisions.
            auto &record=records[i%(state%2 ? 32u:384u)];
            if (i%47u==0u) record[random()%record.size()]^=0x80u;
            const VertexByteView memory{record.data(),record.size()};
            Vertex reference{},a{},b{}; bool generic_hit=false,fixed_hit=false;
            const auto decode=[&](Vertex &v) {
                return decode_vertex_0115_ambient_fast(memory,0u,layout,commands,
                    transform,v,error,lighting,colors);
            };
            assert(decode(reference));
            assert(generic.decode(record.data(),10u,a,generic_hit,decode));
            assert(specialized.decode<10u>(record.data(),10u,b,fixed_hit,decode));
            assert(generic_hit==fixed_hit);
            same(reference,a);same(reference,b);
            ++cases;hits+=fixed_hit;misses+=!fixed_hit;
        }
    }
    assert(hits>10000 && misses>10000);
    // Unknown revision and layout changes invalidate even identical bytes.
    assert(!specialized.begin_state(0,layout.type));
    assert(!specialized.begin_state(0,layout.type));
    assert(!specialized.begin_state(24,0x121));
    assert(!specialized.begin_state(24,layout.type));
    Vertex out{}; bool hit=true; unsigned attempts=0;
    for (unsigned i=0;i<2;++i)
        assert(!specialized.decode<10u>(records[0].data(),10u,out,hit,
            [&](Vertex &) {++attempts;return false;}));
    assert(attempts==2 && !hit);
    // Mismatched and unavailable input must preserve generic fallback and not
    // read beyond the supplied record (also exercised under ASAN).
    const std::array<std::uint8_t,1> short_record{7};
    assert(specialized.decode<10u>(short_record.data(),1u,out,hit,
        [](Vertex &v) {v.x=7;return true;}) && !hit && out.x==7);
    assert(specialized.decode<10u>(nullptr,10u,out,hit,
        [](Vertex &) {return true;}) && !hit);
    std::cout<<"PASS: "<<cases<<" bit-exact 0x115 vertices; fixed/generic hit decisions identical; "
        "hits="<<hits<<" misses="<<misses<<"; record mutations/collisions, state/layout/unknown "
        "revision invalidation, decode failures and short/null records\n";
}
