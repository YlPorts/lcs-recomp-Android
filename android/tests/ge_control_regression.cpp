// Real GE control-flow executor and real bounds reader. Capture only submitted draws.
#include "../../lcs/host/ge_renderer.cpp"
#include "../../lcs/host/lcs_ge_exec.hpp"
#include <cassert>
#include <iostream>
#include <vector>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept {return 0;}
const char *runtime_thread_name() noexcept {return "test";}
std::uint32_t runtime_dispatch_pc() noexcept {return 0;}
}
namespace lcs {
struct Capture { std::uint32_t va{},ia{},color{}; };
std::vector<Capture> captures;
bool test_capture_primitive(psprecomp::GuestMemory &,const std::array<std::uint32_t,256> &cmd,
 const GeTransformState &,std::uint32_t va,std::uint32_t ia,std::uint32_t,
 GeRenderStats &stats,std::string &,std::uint32_t,std::uint64_t,std::uint64_t,std::uint64_t,bool) {
 captures.push_back({va,ia,cmd[0x55]});stats.next_vertex_address=va;stats.next_index_address=ia;return true;
}
void test_flush(psprecomp::GuestMemory &) {}
}
using namespace lcs;
static std::uint32_t op(unsigned code,std::uint32_t value) {return (code<<24)|(value&0xffffffu);}
static std::uint32_t fl(float f) {return std::bit_cast<std::uint32_t>(f)>>8;}
int main(int argc,char**argv) {
 psprecomp::GuestMemory m;
 const std::uint32_t root=0x08001000, sub=0x08003000, nested=0x08006000, va=0x08010000;
 auto write=[&](auto address,const std::vector<std::uint32_t> &w) {for(auto x:w){m.store32(address,x);address+=4;}};
 auto run=[&](const std::vector<std::uint32_t> &w){captures.clear();write(root,w);execute_ge_list_rendered(m,root);};
 const auto prim=op(4,0x30003), end=op(0x0c,0);
 if(argc<2 || std::string(argv[1])=="call") {
   // The subroutine changes offset. RET must restore its caller's offset, twice.
   write(sub,{op(0x13,0x10),op(0x0a,(nested-0x1000)&0xffffffu),op(0x01,0x10000),prim,op(0x0b,0)});
   write(nested,{op(0x13,0x20),op(0x01,0x10000),prim,op(0x0b,0)});
   run({op(0x10,0x080000),op(0x13,0),op(0x0a,sub),op(0x01,va),prim,end});
   if(captures.size()!=3 || captures[0].va!=va+0x2000 || captures[1].va!=va+0x1000 || captures[2].va!=va) {
       std::cerr<<"FAIL: CALL/RET did not restore nested offset registers\n";return 1;
   }
   // ORIGIN is the command address, not the post-increment PC.
   run({op(0x10,0),op(0x13,0),op(0x14,0),op(0x01,0x40),prim,end});
   assert(captures.size()==1 && captures[0].va==root+8+0x40);
   std::cout<<"PASS: actual nested CALL/RET restores offset, ORIGIN uses exact command PC\n";
 }
 if(argc<2 || std::string(argv[1])=="bbox") {
   // BBOX count 0 sets false; conditional jump skips one material and draw.
   run({op(0x10,0x080000),op(0x13,0),op(0x07,0),op(0x09,root+6*4),op(0x55,0xff00),prim,op(0x55,0x112233),prim,end});
   if(captures.size()!=1 || captures[0].color!=0x112233){std::cerr<<"FAIL: BBOX/BJUMP ignored\n";return 2;}
   std::vector<std::uint32_t> base{op(0x10,0x080000),op(0x13,0),op(0x12,3u<<7),op(0x17,0),op(0x1f,0),op(0x1c,1)};
   const std::array<float,12> identity{1,0,0,0,1,0,0,0,1,0,0,0};
   for(auto pair:{std::pair{0x3a,0x3b},std::pair{0x3c,0x3d}}) {
       base.push_back(op(pair.first,0));for(auto f:identity)base.push_back(op(pair.second,fl(f)));
   }
   base.push_back(op(0x3e,0));
   for(unsigned i=0;i<16;i++)base.push_back(op(0x3f,fl(i%5==0 ? 1.f:0.f)));
   base.push_back(op(1,va));base.push_back(op(7,8));
   const auto jump_slot=base.size();base.push_back(0);base.push_back(prim);base.push_back(end);
   base[jump_slot]=op(9,root+static_cast<unsigned>(base.size()-1)*4);
   for (unsigned sample=0;sample<80;sample++) {
       const bool outside=sample%2==0;
       for(unsigned i=0;i<8;i++){
           float x=(outside?-3.f:0.f)+((i&1)?0.25f:-0.25f);
           float y=(i&2)?0.25f:-0.25f,z=(i&4)?0.25f:-0.25f;
           m.store32(va+i*12,std::bit_cast<std::uint32_t>(x));
           m.store32(va+i*12+4,std::bit_cast<std::uint32_t>(y));
           m.store32(va+i*12+8,std::bit_cast<std::uint32_t>(z));
       }
       run(base);assert(captures.size()==(outside?0u:1u));
       if(!outside)assert(captures[0].va==va+8*12);
   }
   // Direct evaluator is conservative for a volume crossing both side planes.
   std::array<std::uint32_t,256> cmd{};cmd[0x12]=3u<<7;cmd[0x1c]=1;
   GeTransformState tr; reset_ge_transform_state(tr);
   for(unsigned i=0;i<8;i++)m.store32(va+i*12,std::bit_cast<std::uint32_t>((i&1)?3.f:-3.f));
   GeBoundingBoxResult result;std::string error;
   assert(test_ge_bounding_box(m,cmd,tr,va,0,8,result,error) && result.visible);
   cmd[0x12]|=1u<<23;
   assert(test_ge_bounding_box(m,cmd,tr,va,0,8,result,error) && result.visible);
   std::cout<<"PASS: BBOX/BJUMP real commands, 80 inside/outside volumes, stream advancement, crossing-volume and through-mode conservative fallback\n";
 }
}
