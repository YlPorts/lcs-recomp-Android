// Exercise the real GE command executor and real vertex/texture readers.
// Only primitive submission and flush are replaced, to inspect the latched state.
#include "../../lcs/host/ge_renderer.cpp"
#include "../../lcs/host/lcs_ge_exec.hpp"
#include <iostream>
#include <cassert>
#include <vector>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept {return 0;}
const char *runtime_thread_name() noexcept {return "test";}
std::uint32_t runtime_dispatch_pc() noexcept {return 0;}
}
namespace lcs {
static GeTransformState captured;
static std::array<std::uint32_t,256> captured_commands;
bool test_capture_primitive(psprecomp::GuestMemory &,const std::array<std::uint32_t,256> &cmd,
 const GeTransformState &tr,std::uint32_t va,std::uint32_t ia,std::uint32_t,
 GeRenderStats &stats,std::string &,std::uint32_t,std::uint64_t,std::uint64_t,std::uint64_t,bool) {
 captured=tr;captured_commands=cmd;stats.next_vertex_address=va;stats.next_index_address=ia;return true;
}
void test_flush(psprecomp::GuestMemory &) {}
}
using namespace lcs;
static std::uint32_t f24(float f) {return std::bit_cast<std::uint32_t>(f)>>8;}
static bool equal(Color a, Color b) {return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;}
int main(int argc,char**argv) {
 psprecomp::GuestMemory memory;
 constexpr std::uint32_t code=0x08001000u,palette=0x08010000u,texture=0x08012000u;
 auto run=[&](const std::vector<std::uint32_t> &words) {
   for (std::size_t i=0;i<words.size();i++) memory.store32(code+static_cast<std::uint32_t>(i)*4u,words[i]);
   memory.store32(code+static_cast<std::uint32_t>(words.size())*4u,0x04030003u);
   memory.store32(code+static_cast<std::uint32_t>(words.size()+1)*4u,0x0c000000u);
   execute_ge_list_rendered(memory,code);
 };
 const std::array<float,12> matrix{2,0,0,0,3,0,0,0,1,0.25f,0.5f,1};
 std::vector<std::uint32_t> words{0x40000000u};
 for (float f:matrix)words.push_back(0x41000000u|f24(f));
 run(words);
 if (!(argc>1 && std::string(argv[1])=="palette")) {
   if (captured.texture!=matrix) {std::cout<<"FAIL: GE texture matrix commands were discarded\n";return 1;}
   auto point=transform_4x3(captured.texture,{0.5f,0.25f,0});
   assert(point.x==1.25f && point.y==1.25f && point.z==1.0f);
   std::cout<<"PASS: real GE list executes TEXMTXNUM/TEXMTXDATA, including repeated zeros\n";
 }
 for (unsigned i=0;i<256;i++)memory.store32(palette+i*4u,0xff0000ffu); // Red
 run({0xb0000000u|(palette&0x00ffffffu),0xb1000000u|((palette>>8)&0xf0000u),0xc4000020u});
 const auto first_snapshot=captured.clut.snapshot;
 const auto checksum=captured.clut.checksum;
 if (!first_snapshot || (*first_snapshot)[0]!=255 || (*first_snapshot)[1]!=0) {
   std::cout<<"FAIL: CLUTLOAD did not retain palette bytes\n";return 1;
 }
 auto cmd=captured_commands;
 cmd[0xc3]=5;cmd[0xc5]=3u|(255u<<8);cmd[0xa0]=texture&0x00ffffffu;cmd[0xa8]=1|((texture>>8)&0xf0000u);cmd[0xb8]=0;
 memory.store8(texture,1);
 auto before=make_texture_setup_for_level(memory,cmd,0,&captured.clut);
 assert(equal(sample_texture_nearest(memory,before,0,0),{255,0,0,255}));
 for (unsigned i=0;i<256;i++)memory.store32(palette+i*4u,0xff00ff00u); // RAM now green
 run({});
 assert(captured.clut.snapshot==first_snapshot && captured.clut.checksum==checksum);
 auto unchanged=make_texture_setup_for_level(memory,cmd,0,&captured.clut);
 assert(equal(sample_texture_nearest(memory,unchanged,0,0),{255,0,0,255}));
 run({0xc4000001u}); // Partial 32-byte update, upper palette entries stay red
 auto after=make_texture_setup_for_level(memory,cmd,0,&captured.clut);
 assert(equal(sample_texture_nearest(memory,after,0,0),{0,255,0,255}));
 assert(equal(read_clut_fast(memory,after,20),{255,0,0,255}));
 assert(equal(sample_texture_nearest(memory,before,0,0),{255,0,0,255})); // queued old snapshot
 auto last=captured.clut.snapshot;run({0xc4000000u});assert(captured.clut.snapshot==last);
 // All four indexed formats read the same palette with mask/shift/start.
 for (unsigned fmt=4;fmt<=7;fmt++) {
   cmd[0xc3]=fmt;memory.store32(texture,1);
   const auto t=make_texture_setup_for_level(memory,cmd,0,&captured.clut);
   assert(equal(sample_texture_nearest(memory,t,0,0),{0,255,0,255}));
 }
 // 16-bit CLUT can access upper 256 entries too.
 for (unsigned i=0;i<512;i++)memory.store16(palette+2*i,0xffffu);
 memory.store16(palette+2*511,0x801fu);
 run({0xc4000020u});cmd[0xc5]=1u|(15u<<8)|(31u<<16);cmd[0xc3]=5;
 memory.store8(texture,15);auto last16=make_texture_setup_for_level(memory,cmd,0,&captured.clut);
 assert(equal(sample_texture_nearest(memory,last16,0,0),{255,0,0,255}));
 std::cout<<"PASS: real CLUTLOAD retains bytes across RAM writes; partial/zero loads, queued snapshots, T4/T8/T16/T32, 16-bit entry 511\n";
}
