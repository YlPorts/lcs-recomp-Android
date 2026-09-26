// Replay the actual CPU vertex stage from capture_vertex_fixture.py inputs.
// Build with LCS_BASELINE_REPLAY and LCS_TEST_RENDERER pointing at an unmodified
// renderer to write a reference. No captured game data belongs in the repo.
#ifndef LCS_TEST_RENDERER
#define LCS_TEST_RENDERER "../../lcs/host/ge_renderer.cpp"
#endif
#include LCS_TEST_RENDERER
#include <cassert>
#include <fstream>
#include <iostream>

namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return 0; }
const char *runtime_thread_name() noexcept { return "capture-vertex-replay"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}
using namespace lcs;

struct CapturedDraw {
    std::uint32_t primitive{}, vertex_revision{}, lighting_revision{};
    std::array<std::uint32_t,256> commands{};
    GeTransformState transform{};
    VertexLayout layout{};
    std::vector<std::uint8_t> records,indices;
};
static void read_bytes(std::istream &input, void *data, std::size_t bytes) {
    input.read(static_cast<char *>(data),static_cast<std::streamsize>(bytes));
    if (!input) throw std::runtime_error("Incomplete capture fixture/reference");
}
static std::vector<CapturedDraw> load_fixture(const char *path) {
    static_assert(std::endian::native==std::endian::little);
    std::ifstream file(path,std::ios::binary);
    char magic[8]; read_bytes(file,magic,8);
    if (std::memcmp(magic,"LCSVTX1\0",8)!=0) throw std::runtime_error("Invalid vertex fixture");
    std::uint32_t count{}; read_bytes(file,&count,4);
    if (count==0 || count>100000) throw std::runtime_error("Invalid draw count");
    std::vector<CapturedDraw> draws(count);
    for (auto &draw:draws) {
        std::array<std::uint32_t,5> header{}; read_bytes(file,header.data(),sizeof(header));
        draw.primitive=header[0]; draw.vertex_revision=header[3]; draw.lighting_revision=header[4];
        if (header[1]>4*1024*1024 || header[2]>65535*4)
            throw std::runtime_error("Oversized vertex inputs");
        read_bytes(file,draw.commands.data(),sizeof(draw.commands));
        reset_ge_transform_state(draw.transform);
        for (auto part:{std::span<float>(draw.transform.bones),std::span<float>(draw.transform.world),
             std::span<float>(draw.transform.view),std::span<float>(draw.transform.projection),
             std::span<float>(draw.transform.texture),std::span<float>(draw.transform.morph_weights)})
            read_bytes(file,part.data(),part.size_bytes());
        draw.records.resize(header[1]); draw.indices.resize(header[2]);
        read_bytes(file,draw.records.data(),draw.records.size());
        read_bytes(file,draw.indices.data(),draw.indices.size());
        std::string error;
        if (!build_vertex_layout_cached(draw.commands[0x12],draw.layout,error))
            throw std::runtime_error(error);
    }
    return draws;
}

// Avoid padding bytes: these are all observable decoder outputs, including
// exact float bits. The ultrawide correction happens after this CPU stage.
static std::array<std::uint32_t,10> vertex_bits(const Vertex &v) {
    return {std::bit_cast<std::uint32_t>(v.color),std::bit_cast<std::uint32_t>(v.x),
        std::bit_cast<std::uint32_t>(v.y),std::bit_cast<std::uint32_t>(v.z),
        std::bit_cast<std::uint32_t>(v.w),std::bit_cast<std::uint32_t>(v.inv_w),
        std::bit_cast<std::uint32_t>(v.u),std::bit_cast<std::uint32_t>(v.v),
        std::bit_cast<std::uint32_t>(v.q),std::bit_cast<std::uint32_t>(v.fog_factor)};
}
struct ReplayResult {
    std::uint64_t vertices{},memo_hits{},indexed_hits{},fast_candidates{},checksum{};
};
template <typename Observe>
static ReplayResult replay(const std::vector<CapturedDraw> &draws, Observe observe) {
    ReplayResult result;
    PreparedLighting lighting{};
    VertexLightingCache light_cache;
    GeVertexMemo<Vertex> memo;
#ifndef LCS_BASELINE_REPLAY
    AmbientVertexColorCache ambient_cache;
#endif
    std::uint32_t light_revision=0;
    std::vector<Vertex> vertices;
    for (const auto &draw:draws) {
        const auto &layout=draw.layout;
        const auto &commands=draw.commands;
        const auto count=draw.primitive&65535u;
        if (draw.lighting_revision!=light_revision) {
            lighting=prepare_lighting(layout.color_type>=4u,commands);
            light_cache.begin(lighting); light_revision=draw.lighting_revision;
#ifndef LCS_BASELINE_REPLAY
            ambient_cache.begin(lighting);
#endif
        }
        memo.begin_state(draw.vertex_revision,layout.type);
        const bool memo_enabled=layout.index_type==0 && !layout.through && count>=6u &&
            layout.stride<=64u && (lighting.enabled || layout.weight_type || layout.morph_count>1);
        const auto uv_generation=commands[0xc0]&3u;
        const bool environment_uv=!layout.through && uv_generation==2;
        const auto environment=environment_uv ? prepare_environment_map(commands):PreparedEnvironmentMap{};
#ifndef LCS_BASELINE_REPLAY
        const bool ambient_0115=ambient_0115_eligible(layout,uv_generation,true,ambient_cache);
#endif
        std::array<std::uint32_t,256> reuse_indices{};
        std::array<bool,256> reuse_valid{};
        std::array<Vertex,256> reuse_vertices;
        const VertexByteView index_view{draw.indices.data(),draw.indices.size()};
        vertices.clear(); vertices.reserve(count);
        for (std::uint32_t n=0;n<count;++n) {
            std::uint32_t index=n;
            if (layout.index_type) {
                const auto offset=n*index_size(layout.index_type);
                if (!index_view.contains(offset,index_size(layout.index_type)))
                    throw std::runtime_error("Captured index outside record");
                index=layout.index_type==1 ? index_view.aot_load8(offset):
                    layout.index_type==2 ? index_view.aot_load16(offset):index_view.aot_load32(offset);
            }
            const auto slot=index&255u;
            Vertex vertex{};
            if (layout.index_type && reuse_valid[slot] && reuse_indices[slot]==index) {
                vertex=reuse_vertices[slot]; ++result.indexed_hits;
            } else {
                const auto offset=std::uint64_t(index)*layout.stride;
                if (offset+layout.stride>draw.records.size())
                    throw std::runtime_error("Captured vertex outside record");
                const auto *record=draw.records.data()+offset;
                const VertexByteView view{record,layout.stride};
                std::string error;
                const auto decode=[&](Vertex &out) {
#ifndef LCS_BASELINE_REPLAY
                    if (ambient_0115) {
                        ++result.fast_candidates;
                        return decode_vertex_0115_ambient_fast(view,0u,layout,commands,
                            draw.transform,out,error,lighting,ambient_cache);
                    }
#endif
                    return decode_vertex_optimized(view,0u,layout,commands,draw.transform,out,error,
                        &lighting,&light_cache,environment_uv ? &environment:nullptr);
                };
                bool hit=false;
                const bool ok=memo_enabled ? memo.decode(record,layout.stride,vertex,hit,decode):decode(vertex);
                if (!ok) throw std::runtime_error(error);
                result.memo_hits+=hit;
                if (layout.index_type) {
                    reuse_vertices[slot]=vertex; reuse_indices[slot]=index; reuse_valid[slot]=true;
                }
            }
            vertices.push_back(vertex);
            ++result.vertices;
            result.checksum+=std::bit_cast<std::uint32_t>(vertex.color);
            result.checksum+=std::bit_cast<std::uint32_t>(vertex.x);
            result.checksum+=std::bit_cast<std::uint32_t>(vertex.u);
            observe(vertex);
        }
    }
    return result;
}

int main(int argc,char **argv) {
    if (argc<2) { std::cerr<<"usage: capture_vertex_replay fixture.bin [--write-reference|--compare reference.bin]\n"; return 2; }
    try {
        const auto draws=load_fixture(argv[1]);
        if (argc==4 && std::string(argv[2])=="--write-reference") {
            std::ofstream output(argv[3],std::ios::binary);
            const auto r=replay(draws,[&](const Vertex &v) {
                const auto bits=vertex_bits(v);
                output.write(reinterpret_cast<const char *>(bits.data()),sizeof(bits));
            });
            if (!output) throw std::runtime_error("Could not write baseline output");
            std::cout<<"BASELINE: saved "<<r.vertices<<" exact vertex outputs\n";
        } else if (argc==4 && std::string(argv[2])=="--compare") {
            std::ifstream input(argv[3],std::ios::binary); std::uint64_t index=0;
            const auto r=replay(draws,[&](const Vertex &v) {
                std::array<std::uint32_t,10> reference{};
                read_bytes(input,reference.data(),sizeof(reference));
                if (reference!=vertex_bits(v))
                    throw std::runtime_error("Baseline mismatch at vertex "+std::to_string(index));
                ++index;
            });
            if (input.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Extra baseline vertices");
            std::cout<<"PASS: "<<draws.size()<<" captured draws / "<<r.vertices
                <<" vertices bit-exact against unmodified baseline; memoHits="<<r.memo_hits
                <<" indexedHits="<<r.indexed_hits<<" fastDecodes="<<r.fast_candidates<<"\n";
        }
        constexpr unsigned frames=10,trials=7;
        std::array<double,trials> timings{}; ReplayResult result{};
        for (unsigned trial=0;trial<trials;++trial) {
            const auto begin=std::chrono::steady_clock::now();
            for (unsigned frame=0;frame<frames;++frame) result=replay(draws,[](const Vertex &){});
            timings[trial]=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-begin).count()/frames;
        }
        std::sort(timings.begin(),timings.end());
        std::cout<<"HOST CAPTURE CPU REPLAY: draws="<<draws.size()<<" vertices="<<result.vertices
            <<" medianMs="<<timings[trials/2]<<" minMs="<<timings[0]<<" maxMs="<<timings.back()
            <<" memoHits="<<result.memo_hits<<" indexedHits="<<result.indexed_hits
            <<" fastDecodes="<<result.fast_candidates<<" checksum="<<result.checksum
            <<"; 7 trials x10 full captured CPU vertex frames; NOT Android/game FPS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1;
    }
}
