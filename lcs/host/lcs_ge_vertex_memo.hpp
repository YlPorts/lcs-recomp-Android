#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace lcs {
template<class Vertex> class GeVertexMemo {
    struct Entry { std::uint32_t generation{}; std::uint32_t size{};
        std::array<std::uint8_t,64> bytes{}; Vertex vertex{}; };
    std::array<Entry,256> slots_{};
    std::uint32_t generation_{};
public:
    void begin() noexcept {
        if (++generation_ == 0) {
            for (auto &s:slots_) s.generation=0;
            generation_=1;
        }
    }
    template<class Decode> bool decode(const std::uint8_t *record, std::uint32_t size,
                                      Vertex &out, bool &hit, Decode &&decode) {
        hit=false;
        if (!record || size==0 || size>64) return decode(out);
        std::uint64_t hash=0x9E3779B97F4A7C15ull;
        std::uint32_t i=0;
        for (;i+8<=size;i+=8) {
            std::uint64_t word;std::memcpy(&word,record+i,8);
            hash=(hash^word)*0xBF58476D1CE4E5B9ull;
        }
        for (;i<size;++i) hash=(hash^record[i])*0x100000001B3ull;
        auto &e=slots_[(hash^(hash>>32))&255u];
        if (e.generation==generation_ && e.size==size && std::memcmp(e.bytes.data(),record,size)==0) {
            out=e.vertex;hit=true;return true;
        }
        if (!decode(out)) return false;
        e.generation=generation_;e.size=size;e.vertex=out;
        std::memcpy(e.bytes.data(),record,size);return true;
    }
};
}
