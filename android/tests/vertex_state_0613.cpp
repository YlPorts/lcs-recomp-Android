#include "../../lcs/host/ge_renderer.cpp"
#include <cassert>
#include <random>
#include <iostream>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept{return 0;}
const char *runtime_thread_name() noexcept{return "test";}
std::uint32_t runtime_dispatch_pc() noexcept{return 0;}
}
using namespace lcs;
void same(const Vertex&a,const Vertex&b){
 const float av[]{a.x,a.y,a.z,a.w,a.u,a.v,a.q,a.inv_w,a.fog_factor};
 const float bv[]{b.x,b.y,b.z,b.w,b.u,b.v,b.q,b.inv_w,b.fog_factor};
 for(unsigned i=0;i<9;i++)assert(std::bit_cast<unsigned>(av[i])==std::bit_cast<unsigned>(bv[i]));
 assert(std::bit_cast<unsigned>(a.color)==std::bit_cast<unsigned>(b.color));
}
int main(){
 psprecomp::GuestMemory mem;std::array<std::uint32_t,256> c{};GeTransformState tr;
 reset_ge_transform_state(tr);VertexLayout layout{};std::string e;assert(build_vertex_layout_cached(0x1ff,layout,e));
 c[0x17]=1;c[0x18]=1;c[0x65]=std::bit_cast<unsigned>(1.f)>>8;c[0x58]=255;
 c[0x55]=0x8090a0;c[0x56]=0xffffff;c[0x5c]=0x102030;c[0x5d]=255;c[0x90]=0x607080;
 constexpr unsigned at=0x08010000,records=16;std::mt19937 rng(613);
 for(unsigned i=0;i<records;++i){
   auto p=at+i*layout.stride;mem.store32(p+layout.color_offset,rng());
   for(unsigned j=0;j<3;j++) {
     mem.store32(p+layout.normal_offset+4*j,std::bit_cast<unsigned>(float(j==2)));
     mem.store32(p+layout.position_offset+4*j,std::bit_cast<unsigned>(float(i+j)/20));
   }
 }
 GeVertexMemo<Vertex> memo;GeStateRevisions revisions;
 unsigned cases=0,hits=0;
 for(unsigned draw=0;draw<2000;++draw){
   if(draw%31==0)revisions.begin_list();
   if(draw%23==0){c[0x55]=rng()&0xffffff;revisions.update(0x55,true);}
   if(draw%19==0){tr.world[9]=float(draw%11)/10;revisions.update(0x3b,true);}
   if(draw%17==0){c[0x48]=std::bit_cast<unsigned>(float(draw%7)/10)>>8;revisions.update(0x48,true);}
   revisions.update(0xc4,true);revisions.update(0xa0,true); // no vertex effect
   memo.begin_state(revisions.vertex,layout.type);
   const auto lighting=prepare_lighting(true,c);
   for(unsigned i=0;i<64;++i){
     auto p=at+(i%records)*layout.stride;
     if((draw+i)%97==0)mem.store32(p+layout.color_offset,rng());
     Vertex reference{},actual{};bool hit=false;
     assert(decode_vertex_optimized(mem,p,layout,c,tr,reference,e,&lighting));
     assert(memo.decode(mem.raw_pointer(p,layout.stride),layout.stride,actual,hit,
       [&](Vertex&v){return decode_vertex_optimized(mem,p,layout,c,tr,v,e,&lighting);}));
     same(reference,actual);++cases;hits+=hit;
   }
 }
 assert(hits>cases/2);
 std::cout<<"PASS: "<<cases<<" exact cross-draw vertex comparisons, material/UV/world changes, list boundaries and same-address writes; hits="<<hits<<"\n";
}
