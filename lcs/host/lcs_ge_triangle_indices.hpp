#pragma once
#include <cstdint>
#include <vector>
namespace lcs {
inline void append_ge_triangle_indices(std::vector<std::uint32_t> &out,
        std::uint32_t primitive, std::uint32_t count, std::uint32_t base) {
    if (primitive == 3u) {
        for (std::uint32_t i=0; i+2<count; i+=3) {
            out.push_back(base+i);out.push_back(base+i+1);out.push_back(base+i+2);
        }
    } else if (primitive == 4u) {
        for (std::uint32_t i=0; i+2<count; ++i) {
            out.push_back(base+i+(i&1));out.push_back(base+i+1-(i&1));out.push_back(base+i+2);
        }
    } else if (primitive == 5u) {
        for (std::uint32_t i=1; i+1<count; ++i) {
            out.push_back(base);out.push_back(base+i);out.push_back(base+i+1);
        }
    }
}
}
