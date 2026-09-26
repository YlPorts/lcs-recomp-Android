#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <bit>
#include <cstring>
namespace lcs {
// TEXBUFW is a pixel count, but direct/indexed texture rows occupy whole
// 16-byte units. Low stride bits are ignored; a zero encoded stride still
// occupies one unit. Compressed formats retain their pixel stride bits.
// Verified against PPSSPP GetTextureBufw at commit
// 46d5fae34207fc1b793215da46f7c41ee7931590, GPU/Common/TextureDecoder.cpp.
inline std::uint32_t ge_texture_buffer_width(std::uint32_t register_value,
                                             std::uint32_t format) noexcept {
    constexpr std::array<std::uint32_t, 11> pixels_per_unit{
        8u, 8u, 8u, 4u, 32u, 16u, 8u, 4u, 32u, 16u, 16u};
    auto width = register_value & 0x7ffu;
    if (format >= pixels_per_unit.size()) return width ? width : 1u;
    const auto unit = pixels_per_unit[format];
    if (format < 8u) width &= ~(unit - 1u);
    return width ? width : unit;
}
// All fields consulted by the CPU vertex decoder. Texture image/sampler/CLUT
// changes do not affect its output. Start of a GE list always invalidates reuse.
struct GeStateRevisions {
    std::uint64_t draw{1}, vertex{1}, lighting{1};
    void begin_list() noexcept { ++draw; ++vertex; ++lighting; }
    void update(std::uint32_t op, bool changed) noexcept {
        const bool matrix=op>=0x2A && op<=0x41;
        if (!changed && !matrix) return;
        if (op>=0x12 && !matrix) ++draw;
        const bool lights=(op>=0x53 && op<=0x9A) || op==0x17 ||
                          (op>=0x18 && op<=0x1B);
        if (lights) ++lighting;
        if (matrix || lights || op==0x12 || op==0x1F ||
            (op>=0x42 && op<=0x4D) || op==0x51 ||
            (op>=0xB8 && op<=0xBF) || op==0xC0 || op==0xC1 || op==0xCD || op==0xCE)
            ++vertex;
    }
};
// The 4-bit format may select another 16-entry palette at each mip level.
// Apply start/mask/shift first; bank selection is an addition, not an OR.
inline std::uint32_t ge_palette_index(std::uint32_t raw, std::uint32_t shift,
    std::uint32_t mask,std::uint32_t start,std::uint32_t wrap,std::uint32_t bank) noexcept {
    return ((((raw>>(shift&31u))&mask)|(start&wrap))+bank)&wrap;
}
inline std::uint64_t ge_hash_all_bytes(const std::uint8_t *data,std::size_t size) noexcept {
    // Four independent lanes cover every byte, including dynamic atlas updates
    // in regions skipped by the old large-texture sample hash.
    constexpr std::uint64_t mul=0xD6E8FEB86659FD93ull;
    std::array<std::uint64_t,4> h{0x243F6A8885A308D3ull,0x13198A2E03707344ull,
                                0xA4093822299F31D0ull,0x082EFA98EC4E6C89ull};
    std::size_t pos=0;
    for (;pos+32<=size;pos+=32) for (unsigned lane=0;lane<4;++lane) {
        std::uint64_t word; std::memcpy(&word,data+pos+lane*8,8);
        h[lane]=std::rotl(h[lane]^word,27)*mul;
    }
    auto value=h[0]^std::rotl(h[1],13)^std::rotl(h[2],29)^std::rotl(h[3],47)^size;
    for (;pos<size;++pos) value=(value^data[pos])*0x100000001B3ull;
    value^=value>>32;value*=mul;value^=value>>29;
    return value ? value : 1u;
}
inline std::uint64_t ge_palette_signature(const std::array<std::uint8_t,1024> &bytes,
    std::uint32_t format,std::uint32_t palette_format,std::uint32_t shift,
    std::uint32_t mask,std::uint32_t start,std::uint32_t levels,bool shared) noexcept {
    if (format<4 || format>7) return 0;
    const auto entry_bytes=palette_format==3 ? 4u:2u;
    const auto wrap=palette_format==3 ? 255u:511u;
    const auto bits=format==4?4u:format==5?8u:format==6?16u:32u;
    const auto available=shift>=bits?0u:((bits-shift>=8)?255u:((1u<<(bits-shift))-1u));
    const auto possible=available & mask;
    std::uint64_t hash=0xCBF29CE484222325ull;
    const auto count= format==4 && !shared ? levels:1u;
    for (std::uint32_t level=0;level<count;++level) {
        for (std::uint32_t value=0;value<=possible;++value) {
            if ((value & possible)!=value) continue;
            const auto index=((value|(start&wrap))+(shared?0u:level*16u))&wrap;
            for (unsigned b=0;b<entry_bytes;++b)
                hash=(hash^bytes[index*entry_bytes+b])*0x100000001B3ull;
        }
    }
    return hash ? hash:1u;
}
} // namespace lcs
