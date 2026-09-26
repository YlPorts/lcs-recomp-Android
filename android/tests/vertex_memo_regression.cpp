// Tests the real vertex decoder with exact per-primitive record reuse.
#include "../../lcs/host/ge_renderer.cpp"
#include "../../lcs/host/lcs_ge_triangle_indices.hpp"
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
static void same(const Vertex&a,const Vertex&b) {
    assert(std::bit_cast<std::uint32_t>(a.color)==std::bit_cast<std::uint32_t>(b.color));
    const float av[]{a.u,a.v,a.q,a.x,a.y,a.z,a.w,a.inv_w,a.fog_factor};
    const float bv[]{b.u,b.v,b.q,b.x,b.y,b.z,b.w,b.inv_w,b.fog_factor};
    for(unsigned i=0;i<std::size(av);++i) assert(std::bit_cast<std::uint32_t>(av[i])==std::bit_cast<std::uint32_t>(bv[i]));
}
int main() {
    psprecomp::GuestMemory mem; std::array<std::uint32_t,256> cmd{};
    GeTransformState tr{};reset_ge_transform_state(tr);
    VertexLayout layout{};std::string error; assert(build_vertex_layout_cached(0x1ffu,layout,error));
    assert(layout.stride<=64);
    cmd[0x17]=1;cmd[0x18]=1;cmd[0x65]=std::bit_cast<std::uint32_t>(1.f)>>8;
    cmd[0x58]=255;cmd[0x55]=0x778899;cmd[0x56]=0xffffff;cmd[0x5c]=0x303030;cmd[0x5d]=255;cmd[0x90]=0x8090a0;
    std::mt19937 rng(612);std::uniform_real_distribution<float> unit(-1,1);
    constexpr std::uint32_t address=0x08001000;
    constexpr unsigned records=32;
    for(unsigned i=0;i<records;i++) {
        const auto at=address+i*layout.stride;mem.store32(at+layout.color_offset,rng());
        for(unsigned j=0;j<3;j++) {
            mem.store32(at+layout.position_offset+4*j,std::bit_cast<std::uint32_t>(unit(rng)));
            mem.store32(at+layout.normal_offset+4*j,std::bit_cast<std::uint32_t>(unit(rng)));
        }
        mem.store32(at+layout.tc_offset,std::bit_cast<std::uint32_t>(unit(rng)));
        mem.store32(at+layout.tc_offset+4,std::bit_cast<std::uint32_t>(unit(rng)));
    }
    GeVertexMemo<Vertex> memo;unsigned hits=0,cases=0;
    for(unsigned primitive=0;primitive<60;primitive++) {
        cmd[0x55]=rng()&0xffffff;cmd[0x5f]=primitive%3;
        auto light=prepare_lighting(true,cmd);memo.begin();
        for(unsigned n=0;n<1024;n++) {
            const auto at=address+(n%records)*layout.stride;
            if(n%127==0)mem.store32(at+layout.color_offset,rng());
            Vertex reference{},actual{};bool hit=false;
            assert(decode_vertex_optimized(mem,at,layout,cmd,tr,reference,error,&light));
            assert(memo.decode(mem.raw_pointer(at,layout.stride),layout.stride,actual,hit,[&](Vertex&out){
                return decode_vertex_optimized(mem,at,layout,cmd,tr,out,error,&light);
            }));
            same(reference,actual);hits+=hit;cases++;
        }
    }
    assert(hits>20000);
    std::cout<<"PASS: "<<cases<<" exact full-vertex record reuse comparisons; changed records and primitive/material boundaries; hits="<<hits<<"\n";
    // Collision safety and malformed records: only successful decodes are stored.
    Vertex out{};bool hit=false;unsigned attempts=0;memo.begin();
    const auto *p=mem.raw_pointer(address,layout.stride);
    for(unsigned n=0;n<2;n++)assert(!memo.decode(p,layout.stride,out,hit,[&](Vertex&){++attempts;return false;}));
    assert(attempts==2 && !hit);
    assert(memo.decode(nullptr,0,out,hit,[&](Vertex&){return true;}) && !hit);
    std::vector<std::uint32_t> list;
    for(unsigned primitive=3;primitive<=5;primitive++)for(unsigned count=0;count<100;count++) {
        list.clear();append_ge_triangle_indices(list,primitive,count,17);
        const auto triangles=count<3?0:primitive==3?count/3:count-2;
        assert(list.size()==triangles*3);
        for(unsigned t=0;t<triangles;t++) {
            const auto *x=list.data()+t*3;
            assert(x[0]>=17 && x[1]>=17 && x[2]<17+count);
            if(primitive==3)assert(x[0]==17+t*3 && x[1]==18+t*3 && x[2]==19+t*3);
            if(primitive==4)assert(x[0]==17+t+(t&1) && x[1]==18+t-(t&1) && x[2]==19+t);
            if(primitive==5)assert(x[0]==17 && x[1]==18+t && x[2]==19+t);
        }
    }
    list.clear();append_ge_triangle_indices(list,4,4,0);append_ge_triangle_indices(list,4,4,4);
    assert((list==std::vector<std::uint32_t>{0,1,2,2,1,3,4,5,6,6,5,7}));
    std::cout<<"PASS: 300 triangle list/strip/fan conversions preserve winding, provoking vertices and independent boundaries\n";
    // Informational microbenchmark: cache is enabled only for expensive records.
    const auto light=prepare_lighting(true,cmd);
    constexpr unsigned loops=200000;
    auto run=[&](bool cached){
        memo.begin();std::uint64_t checksum=0;auto begin=std::chrono::steady_clock::now();
        for(unsigned i=0;i<loops;i++) {
            const auto at=address+(i%records)*layout.stride;Vertex v{};bool h;
            auto decode=[&](Vertex&x){return decode_vertex_optimized(mem,at,layout,cmd,tr,x,error,&light);};
            if(cached) assert(memo.decode(mem.raw_pointer(at,layout.stride),layout.stride,v,h,decode));
            else assert(decode(v));
            checksum+=std::bit_cast<std::uint32_t>(v.color);
        }
        return std::pair{std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count(),checksum};
    };
    auto reference=run(false),cached=run(true);assert(reference.second==cached.second);
    std::cout<<"HOST ONLY: repeated records referenceMs="<<reference.first<<" memoMs="<<cached.first<<"; NOT Android or game FPS\n";
}
