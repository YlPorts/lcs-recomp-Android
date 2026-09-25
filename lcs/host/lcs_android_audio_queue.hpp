#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
namespace lcs::android_detail {
// One serialized HLE producer and one AAudio consumer. Published packets are
// immutable. No locks, allocations or waits in mix().
class TimedPcmQueue {
public:
    static constexpr std::size_t kFrames = 256;
    static constexpr std::size_t kPackets = 256;
    struct Packet {
        std::uint64_t start{};
        std::uint32_t generation{};
        std::uint32_t frames{};
        std::array<std::int32_t, kFrames * 2> samples{};
    };
private:
    std::array<Packet, kPackets> packets_{};
    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
    std::size_t offset_{};
public:
    bool push(const Packet &packet) noexcept {
        if (packet.frames == 0 || packet.frames > kFrames) return false;
        const auto w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= kPackets) return false;
        packets_[w % kPackets] = packet;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    std::uint64_t mix(std::span<std::int32_t> out, std::uint64_t first,
                      std::uint32_t generation, std::uint64_t &late) noexcept {
        const auto frames = out.size() / 2;
        std::uint64_t contributed = 0;
        auto r = read_.load(std::memory_order_relaxed);
        std::size_t dst = 0;
        while (dst < frames) {
            if (r == write_.load(std::memory_order_acquire)) break;
            const Packet &p = packets_[r % kPackets];
            if (p.generation != generation || offset_ >= p.frames) {
                offset_ = 0;
                read_.store(++r, std::memory_order_release);
                continue;
            }
            std::uint64_t cursor = p.start + offset_;
            if (cursor < first + dst) {
                const auto skip = static_cast<std::size_t>(std::min<std::uint64_t>(
                    first + dst - cursor, p.frames - offset_));
                offset_ += skip; late += skip; continue;
            }
            if (cursor > first + dst) {
                const auto gap = static_cast<std::size_t>(std::min<std::uint64_t>(
                    cursor - (first + dst), frames - dst));
                dst += gap; continue;
            }
            const auto n = std::min(frames - dst, p.frames - offset_);
            for (std::size_t i = 0; i < n * 2; ++i)
                out[dst * 2 + i] += p.samples[offset_ * 2 + i];
            offset_ += n; dst += n; contributed += n;
        }
        return contributed;
    }
    void reset_stopped() noexcept {
        // Close the stream and stop its consumer before resetting.
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
        offset_ = 0;
    }
};
} // namespace lcs::android_detail
