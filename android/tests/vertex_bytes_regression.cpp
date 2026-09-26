// Compare the actual checked guest-memory path with the new byte-view path.
#define main prior_geometry_tests
#include "cpu_geometry_regression.cpp"
#undef main
int main() {
    if (prior_geometry_tests()!=0) return 1;
    psprecomp::GuestMemory memory;
    std::array<std::uint32_t,256> cmd{};
    GeTransformState tr{};reset_ge_transform_state(tr);
    cmd[0x17]=1;cmd[0x18]=1;cmd[0x5f]=1;cmd[0x65]=f24(1);
    cmd[0x55]=0x778899;cmd[0x56]=0xc0d0e0;cmd[0x58]=255;
    cmd[0x5c]=0x405060;cmd[0x5d]=255;cmd[0x8f]=0x808080;cmd[0x90]=0xffffff;
    cmd[0xb8]=8|(8<<8);cmd[0x48]=cmd[0x49]=f24(1);
    const std::uint32_t types[]{0x115,0x1ff,0x19f,0x103,0x81ff,0x1ff|(1u<<18),0x1ff|(3u<<9),0x80019f};
    std::mt19937 random(0x610);
    std::uniform_real_distribution<float> unit(-1.f,1.f);
    constexpr std::uint32_t base=0x08002000;
    std::uint64_t cases=0;
    for (auto type:types) {
        VertexLayout layout;std::string error;
        assert(build_vertex_layout(type,layout,error));
        for (unsigned n=0;n<1000;n++) {
            // Fill complete records, then constrain float fields to finite data.
            for (unsigned i=0;i<layout.stride;i++)memory.store8(base+i,random()&255u);
            for (unsigned m=0;m<layout.morph_count;m++) {
                const auto p=base+m*layout.one_size;
                for (auto item:{std::pair{layout.tc_type,layout.tc_offset},
                               std::pair{layout.normal_type,layout.normal_offset},
                               std::pair{layout.position_type,layout.position_offset},
                               std::pair{layout.weight_type,layout.weight_offset}}) {
                    if(item.first==3u) {
                        const unsigned count=item.second==layout.tc_offset?2u:3u;
                        for(unsigned k=0;k<count && item.second+k*4+4<=layout.one_size;k++)
                            memory.store32(p+item.second+k*4,std::bit_cast<std::uint32_t>(unit(random)));
                    }
                }
            }
            cmd[0xc0]=n%3;cmd[0x53]=n&7;
            const auto light=prepare_lighting(layout.color_type>=4,cmd);
            Vertex a{},b{};
            assert(decode_vertex_optimized(memory,base,layout,cmd,tr,a,error,&light));
            VertexByteView view{memory.raw_pointer(base,layout.stride),layout.stride};
            assert(view.data);
            assert(decode_vertex_optimized(view,0,layout,cmd,tr,b,error,&light));
            same(a,b);cases++;
            VertexByteView truncated{view.data,layout.stride-1};
            assert(!decode_vertex_optimized(truncated,0,layout,cmd,tr,b,error,&light));
        }
    }
    std::cout<<"PASS: "<<cases<<" vertex byte views match guest decoder; all truncated records rejected\n";
    return 0;
}
