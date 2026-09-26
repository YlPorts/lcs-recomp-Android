// Actual decoder and signatures, not a separate replacement implementation.
#ifndef LCS_TEST_RENDERER
#define LCS_TEST_RENDERER "../../lcs/host/ge_renderer.cpp"
#endif
#include LCS_TEST_RENDERER
#include "lcs_ge_state_policy.hpp"
#include <cassert>
#include <iostream>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept{return 0;}
const char *runtime_thread_name() noexcept{return "test";}
std::uint32_t runtime_dispatch_pc() noexcept{return 0;}
}
using namespace lcs;
int main(int argc,char **argv) {
 psprecomp::GuestMemory mem;
 constexpr std::uint32_t pal=0x08010000, tex=0x08020000;
 std::array<std::uint32_t,256> cmd{};
 cmd[0xc3]=4;cmd[0xc5]=3|(255<<8);cmd[0xc2]=0x10100;cmd[0xc6]=4;
 cmd[0xa0]=tex&0xffffff;cmd[0xa8]=1|((tex>>8)&0xf0000);
 cmd[0xa1]=(tex+16)&0xffffff;cmd[0xa9]=1|((tex>>8)&0xf0000);
 for(unsigned i=0;i<256;++i) mem.store32(pal+4*i,0xff0000ff); // red
 mem.store32(pal+4*16,0xffff0000); // blue in level 1 bank
 mem.store32(pal+4*32,0xff00ff00); // green in level 1, start offset 16
 GeClutState state;load_ge_clut(state,mem,pal,32);
 mem.store8(tex,0);mem.store8(tex+16,0);
 const bool hash_only=argc>1 && std::string(argv[1])=="hash";
 if(!hash_only) {
   auto t=make_texture_setup_for_level(mem,cmd,1,&state);
   auto c=sample_texture_nearest(mem,t,0,0);
   if(c.b!=255 || c.r!=0) {std::cout<<"FAIL: separate T4 mip palette uses bank zero\n";return 1;}
   cmd[0xc5]|=1<<16;t=make_texture_setup_for_level(mem,cmd,1,&state);
   c=sample_texture_nearest(mem,t,0,0);assert(c.g==255 && c.r==0 && c.b==0);
   cmd[0xc2]&=~0x100u;t=make_texture_setup_for_level(mem,cmd,1,&state);
   c=sample_texture_nearest(mem,t,0,0);assert(c.b==255 && c.r==0);
   std::cout<<"PASS: actual T4 mip palettes, shared mode, start plus bank selection\n";
 }
 // A large texture mutation outside every old sampled hash range must invalidate.
 cmd[0xc3]=5;cmd[0xb8]=8|(7<<8);cmd[0xa8]=256|((tex>>8)&0xf0000);
 auto t=make_texture_setup_for_level(mem,cmd,0,&state);
 auto first=texture_source_signature(mem,t);mem.store8(tex+1024,79);
 if(first==texture_source_signature(mem,t)) {std::cout<<"FAIL: atlas update outside sampled regions ignored\n";return 2;}
 std::cout<<"PASS: actual large texture signature detects formerly unsampled atlas update\n";
 auto bytes=state.data();
 auto h=[&](const auto&b,unsigned levels=1,bool shared=true) {
   return ge_palette_signature(b,4,3,0,255,0,levels,shared);
 };
 auto base=h(bytes);bytes[999]^=55;assert(base==h(bytes));
 bytes[5]^=34;assert(base!=h(bytes));bytes[5]^=34;
 auto one=h(bytes,1,false),two=h(bytes,2,false);bytes[16*4]^=53;
 assert(h(bytes,1,false)==one && h(bytes,2,false)!=two);
 for (unsigned bits:{4u,8u,16u,32u}) for(unsigned shift=0;shift<32;++shift) {
   unsigned fmt=bits==4?4:bits==8?5:bits==16?6:7;
   for(unsigned mask:{0u,3u,7u,85u,255u}) {
     auto fingerprint=ge_palette_signature(bytes,fmt,3,shift,mask,32,1,true);
     for(unsigned raw=0;raw<256;++raw) {
       const auto constrained= bits>=8 ? raw : raw&15;
       const auto index=ge_palette_index(constrained,shift,mask,32,255,0);
       auto changed=bytes;changed[index*4]^=13;
       assert(fingerprint!=ge_palette_signature(changed,fmt,3,shift,mask,32,1,true));
     }
   }
 }
 std::cout<<"PASS: effective palette fingerprint excludes unused entries; all tested reachable lookups invalidate\n";
 GeStateRevisions r; r.begin_list();const auto v=r.vertex,l=r.lighting;
 for(unsigned op:{0xa0u,0xc2u,0xc4u,0xc5u,0xc6u,0xc7u,0xc8u,0xc9u,0xdfu,0xe8u})r.update(op,true);
 assert(r.vertex==v && r.lighting==l);
 r.update(0x55,true);assert(r.vertex!=v && r.lighting!=l);
 auto n=r.vertex;r.update(0x48,true);assert(r.vertex!=n);
 n=r.vertex;r.update(0x41,false);assert(r.vertex!=n); // matrix cursor consumed
 n=r.vertex;r.begin_list();assert(r.vertex!=n);
 std::cout<<"PASS: material/UV/matrix/new-list invalidate vertex state; CLUT/sampler do not\n";
}
