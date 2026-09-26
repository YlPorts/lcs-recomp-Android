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
int main(){
    psprecomp::GuestMemory memory;
    std::array<std::uint32_t,256>cmd{};GeTransformState tr;reset_ge_transform_state(tr);
    std::string error;VertexLayout layout;assert(build_vertex_layout_cached(0x1ffu,layout,error));
    constexpr std::uint32_t va=0x08010000u,ia=0x08020000u;
    std::mt19937 rng(15015);std::uniform_real_distribution<float>unit(-1,1);
    unsigned rejected=0;
    for(unsigned test=0;test<6000;++test){
        cmd[0x1c]=test&1;const unsigned count=3+test%57;const float scale=0.4f+(test%7)*0.1f;
        unsigned reference=cmd[0x1c]?63:15;
        tr.world[9]=(test%3==0?3.0f:0.0f);tr.world[10]=test%11==0?3.0f:0.0f;
        tr.view[10]=0.1f;tr.projection[15]=test%13==0?-1.0f:1.0f;
        for(unsigned i=0;i<count;++i){
            const auto address=va+i*layout.stride;
            for(unsigned j=0;j<3;++j)memory.store32(address+layout.position_offset+j*4,std::bit_cast<std::uint32_t>(unit(rng)));
            Vertex v{};assert(decode_vertex(memory,address,layout,cmd,tr,v,error));v.x*=scale;
            if(!(v.w>0)||!std::isfinite(v.x+v.y+v.z+v.w)){reference=0;continue;}
            unsigned out=(v.x< -v.w?1u:0u)|(v.x>v.w?2u:0u)|(v.y< -v.w?4u:0u)|(v.y>v.w?8u:0u);
            if(cmd[0x1c])out|=(v.z< -v.w?16u:0u)|(v.z>v.w?32u:0u);
            reference&=out;
        }
        const bool result=ge_position_only_outside(memory,cmd,tr,layout,va,0,count,scale);
        assert(result==(reference!=0));rejected+=result;
    }
    tr.projection[15]=1;tr.world[9]=3;
    assert(!ge_position_only_outside(memory,cmd,tr,layout,0xffffffffu,0,50,1));
    auto unsupported=layout;unsupported.weight_type=1;
    assert(!ge_position_only_outside(memory,cmd,tr,unsupported,va,0,50,1));
    unsupported=layout;unsupported.morph_count=2;
    assert(!ge_position_only_outside(memory,cmd,tr,unsupported,va,0,50,1));
    unsupported=layout;unsupported.through=true;
    assert(!ge_position_only_outside(memory,cmd,tr,unsupported,va,0,50,1));
    cmd[0xd3]=1;assert(!ge_position_only_outside(memory,cmd,tr,layout,va,0,50,1));cmd[0xd3]=0;
    VertexLayout indexed;assert(build_vertex_layout_cached(0x1ffu|(2u<<11),indexed,error));
    assert(!ge_position_only_outside(memory,cmd,tr,indexed,va,0xffffffffu,50,1));
    for(unsigned i=0;i<50;++i)memory.store16(ia+i*2,49-i);
    assert(ge_position_only_outside(memory,cmd,tr,indexed,va,ia,50,1));
    std::cout<<"PASS: 6000 position pretests match full actual vertex decoder; conservative malformed, skin, morph, clear, 2D and indexed cases. Rejected="<<rejected<<"\n";
}
