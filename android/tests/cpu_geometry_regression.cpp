// Exercise actual renderer helpers, not a substitute algorithm.
#include "../../lcs/host/ge_renderer.cpp"
#include <cassert>
#include <random>
#include <iostream>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept {return 0;}
const char *runtime_thread_name() noexcept {return "test";}
std::uint32_t runtime_dispatch_pc() noexcept {return 0;}
}
using namespace lcs;
static std::uint32_t f24(float f){return std::bit_cast<std::uint32_t>(f)>>8;}
static void same(const Vertex&a,const Vertex&b){
    assert(a.color.r==b.color.r && a.color.g==b.color.g && a.color.b==b.color.b && a.color.a==b.color.a);
    const float x[]{a.x,a.y,a.z,a.w,a.u,a.v,a.q,a.fog_factor},y[]{b.x,b.y,b.z,b.w,b.u,b.v,b.q,b.fog_factor};
    for(int i=0;i<8;i++)assert(std::abs(x[i]-y[i])<1e-5f);
}
int main(){
    psprecomp::GuestMemory mem;std::array<std::uint32_t,256> cmd{};
    GeTransformState tr{};reset_ge_transform_state(tr);std::string error;VertexLayout layout;
    assert(build_vertex_layout_cached(0x1ffu,layout,error));constexpr std::uint32_t addr=0x08001000;
    std::mt19937 rng(0x069);std::uniform_real_distribution<float> unit(-1.f,1.f);
    cmd[0x17]=1;cmd[0x18]=1;cmd[0x5f]=1;cmd[0x65]=f24(1);cmd[0x5b]=f24(2);
    cmd[0x55]=0x778899;cmd[0x56]=0xc0d0e0;cmd[0x57]=0x303030;cmd[0x58]=255;
    cmd[0x5c]=0x405060;cmd[0x5d]=255;cmd[0x8f]=0x808080;cmd[0x90]=0xffffff;cmd[0x91]=0x202020;
    cmd[0xb8]=8|(8<<8);cmd[0x48]=cmd[0x49]=f24(1);
    for(int n=0;n<5000;n++){
        mem.store32(addr+layout.tc_offset,std::bit_cast<std::uint32_t>(unit(rng)));
        mem.store32(addr+layout.tc_offset+4,std::bit_cast<std::uint32_t>(unit(rng)));
        mem.store32(addr+layout.color_offset,rng());
        for(int j=0;j<3;j++){
            mem.store32(addr+layout.normal_offset+j*4,std::bit_cast<std::uint32_t>(unit(rng)));
            mem.store32(addr+layout.position_offset+j*4,std::bit_cast<std::uint32_t>(unit(rng)));
        }
        cmd[0x53]=n&7;cmd[0x5f]=n%3;cmd[0xc0]=n%3;
        Vertex a{},b{};auto light=prepare_lighting(true,cmd);
        assert(decode_vertex(mem,addr,layout,cmd,tr,a,error));
        assert(decode_vertex(mem,addr,layout,cmd,tr,b,error,&light));same(a,b);
    }
    std::cout<<"PASS: 5000 prepared-lighting and UV cases match original path\n";
    FragmentSetup setup{};setup.valid=true;setup.scissor_x1=511;setup.scissor_y1=319;
    cmd[0x1c]=1;cmd[0x1d]=0;cmd[0x50]=1;cmd[0xd3]=0;
    cmd[0x42]=f24(256);cmd[0x43]=f24(-160);cmd[0x44]=f24(32767);
    cmd[0x45]=f24(256);cmd[0x46]=f24(160);cmd[0x47]=f24(32767);
    GeRenderStatsCollectionScope scope(false);
    for(int n=0;n<10000;n++){
        Vertex a{},b{},c{};for(auto v:{&a,&b,&c}){v->x=unit(rng)*2;v->y=unit(rng)*2;v->z=unit(rng)*2;v->w=1;v->color={128,255,30,255};}
        std::vector<PreparedScreenTriangle> fast,reference;GeRenderStats stats{};
        append_prepared_triangles(cmd,setup,a,b,c,c,false,stats,fast,true);
        ClipPolygon polygon;clip_triangle(a,b,c,true,polygon);
        if(polygon.size>=3){
            bool valid=true;for(std::size_t i=0;i<polygon.size;i++)valid&=viewport_transform(polygon.vertices[i],cmd);
            if(valid)for(std::size_t i=1;i+1<polygon.size;i++){
                PreparedScreenTriangle t;
                if(prepare_gpu_only_screen_triangle(cmd,setup,polygon.vertices[0],polygon.vertices[i],polygon.vertices[i+1],c.color,stats,t))reference.push_back(t);
            }
        }
        assert(fast.size()==reference.size());
        for(std::size_t i=0;i<fast.size();i++){same(fast[i].a,reference[i].a);same(fast[i].b,reference[i].b);same(fast[i].c,reference[i].c);}
    }
    std::cout<<"PASS: 10000 triangles match original polygon clipping\n";
    return 0;
}
