#pragma once
#include <cstdint>
#include <unordered_map>
namespace lcs::android_detail {
// Preserve RAM versus VRAM before normalizing uncached/mirrored addresses.
inline bool is_vram_address(std::uint32_t address) noexcept {
    const std::uint32_t physical = address & 0x3FFFFFFFu;
    return physical >= 0x04000000u && physical < 0x04800000u;
}
inline std::uint32_t vram_offset(std::uint32_t address) noexcept {
    return address & 0x001FFFF0u;
}
class TextureVersions {
    struct Entry { std::uint64_t signature{}; std::uint64_t epoch{}; };
    std::unordered_map<std::uint64_t, Entry> entries_;
public:
    void clear() noexcept { entries_.clear(); }
    void observe(std::uint64_t base, std::uint64_t signature, std::uint64_t epoch) {
        if (signature != 0u) entries_[base] = Entry{signature, epoch};
    }
    std::uint64_t resolve(std::uint64_t base, std::uint64_t explicit_signature) const noexcept {
        if (explicit_signature != 0u) return explicit_signature;
        const auto it = entries_.find(base);
        return it == entries_.end() ? 0u : it->second.signature;
    }
    bool needs_check(std::uint64_t base, std::uint64_t epoch) const noexcept {
        const auto it = entries_.find(base);
        return it == entries_.end() || it->second.epoch != epoch;
    }
};
inline bool color_test_pass(unsigned function, std::uint32_t rgb,
                            std::uint32_t reference, std::uint32_t mask) noexcept {
    const bool equal = (rgb & mask & 0xFFFFFFu) == (reference & mask & 0xFFFFFFu);
    switch (function & 3u) {
    case 0u: return false;
    case 1u: return true;
    case 2u: return equal;
    default: return !equal;
    }
}
} // namespace lcs::android_detail
