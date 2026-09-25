#include "android_debug.hpp"

#include <atomic>
#include <cstdio>
#include <sstream>

namespace lcs::android_debug {
namespace {

std::atomic<std::uint32_t> g_stage{static_cast<std::uint32_t>(Stage::Idle)};
std::atomic<int> g_result{0};

std::atomic<std::uint64_t> g_set_fb_calls{0};
std::atomic<std::uint64_t> g_vblanks{0};
std::atomic<std::uint64_t> g_ge_lists{0};
std::atomic<std::uint64_t> g_present_attempts{0};
std::atomic<std::uint64_t> g_swaps{0};
std::atomic<std::uint64_t> g_swap_failures{0};
std::atomic<std::uint64_t> g_egl_failures{0};

std::atomic<std::uint32_t> g_last_fb{0};
std::atomic<std::uint32_t> g_last_stride{0};
std::atomic<std::uint32_t> g_last_format{0};
std::atomic<std::uint32_t> g_last_ge_list{0};
std::atomic<std::uint32_t> g_last_present_buffer{0};
std::atomic<std::uint32_t> g_last_present_width{0};
std::atomic<std::uint32_t> g_last_present_height{0};
std::atomic<std::uint64_t> g_last_non_black{0};
std::atomic<std::uint64_t> g_last_total_pixels{0};
std::atomic<bool> g_egl_ready{false};

const char *stage_name(Stage stage) noexcept {
    switch (stage) {
    case Stage::Idle: return "idle";
    case Stage::NativeEntry: return "native-entry";
    case Stage::MainStarted: return "main";
    case Stage::ElfLoaded: return "elf-loaded";
    case Stage::RuntimeReady: return "runtime-ready";
    case Stage::ProfileInstalled: return "profile-installed";
    case Stage::Running: return "running";
    case Stage::Finished: return "finished";
    }
    return "?";
}

std::string hex32(std::uint32_t value) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "0x%08X", value);
    return text;
}

}  // namespace

void reset() noexcept {
    g_stage.store(static_cast<std::uint32_t>(Stage::Idle));
    g_result.store(0);
    g_set_fb_calls.store(0);
    g_vblanks.store(0);
    g_ge_lists.store(0);
    g_present_attempts.store(0);
    g_swaps.store(0);
    g_swap_failures.store(0);
    g_egl_failures.store(0);
    g_last_fb.store(0);
    g_last_stride.store(0);
    g_last_format.store(0);
    g_last_ge_list.store(0);
    g_last_present_buffer.store(0);
    g_last_present_width.store(0);
    g_last_present_height.store(0);
    g_last_non_black.store(0);
    g_last_total_pixels.store(0);
    g_egl_ready.store(false);
}

void set_stage(Stage stage) noexcept {
    g_stage.store(static_cast<std::uint32_t>(stage), std::memory_order_relaxed);
}

void set_result(int result) noexcept {
    g_result.store(result, std::memory_order_relaxed);
}

void note_set_framebuffer(std::uint32_t address, std::uint32_t stride,
                          std::uint32_t format) noexcept {
    g_set_fb_calls.fetch_add(1u, std::memory_order_relaxed);
    g_last_fb.store(address, std::memory_order_relaxed);
    g_last_stride.store(stride, std::memory_order_relaxed);
    g_last_format.store(format, std::memory_order_relaxed);
}

void note_vblank(std::uint64_t index, std::uint32_t framebuffer) noexcept {
    g_vblanks.store(index, std::memory_order_relaxed);
    if (framebuffer != 0u)
        g_last_fb.store(framebuffer, std::memory_order_relaxed);
}

void note_ge_list(std::uint32_t address) noexcept {
    g_ge_lists.fetch_add(1u, std::memory_order_relaxed);
    g_last_ge_list.store(address, std::memory_order_relaxed);
}

void note_present_attempt(std::uint32_t buffer, std::uint32_t stride,
                          std::uint32_t format, std::uint32_t width,
                          std::uint32_t height) noexcept {
    g_present_attempts.fetch_add(1u, std::memory_order_relaxed);
    g_last_present_buffer.store(buffer, std::memory_order_relaxed);
    g_last_stride.store(stride, std::memory_order_relaxed);
    g_last_format.store(format, std::memory_order_relaxed);
    g_last_present_width.store(width, std::memory_order_relaxed);
    g_last_present_height.store(height, std::memory_order_relaxed);
}

void note_present_pixels(std::uint64_t non_black, std::uint64_t total) noexcept {
    g_last_non_black.store(non_black, std::memory_order_relaxed);
    g_last_total_pixels.store(total, std::memory_order_relaxed);
}

void note_egl_ready(bool ready) noexcept {
    g_egl_ready.store(ready, std::memory_order_relaxed);
    if (!ready) g_egl_failures.fetch_add(1u, std::memory_order_relaxed);
}

void note_swap(bool ok) noexcept {
    if (ok) g_swaps.fetch_add(1u, std::memory_order_relaxed);
    else g_swap_failures.fetch_add(1u, std::memory_order_relaxed);
}

std::string status_text(std::uint32_t pc, std::int32_t thread_uid,
                        const char *thread_name) {
    const auto stage = static_cast<Stage>(g_stage.load(std::memory_order_relaxed));
    const std::uint64_t total = g_last_total_pixels.load(std::memory_order_relaxed);
    const std::uint64_t non_black = g_last_non_black.load(std::memory_order_relaxed);

    std::ostringstream out;
    out << "LCS Android 0.3 DIAG\n"
        << "stage=" << stage_name(stage)
        << "  pc=" << hex32(pc)
        << "  uid=" << thread_uid;
    if (thread_name != nullptr && *thread_name != '\0')
        out << " (" << thread_name << ")";

    out << "\nvblank=" << g_vblanks.load(std::memory_order_relaxed)
        << "  setFB=" << g_set_fb_calls.load(std::memory_order_relaxed)
        << "  fb=" << hex32(g_last_fb.load(std::memory_order_relaxed))
        << "  stride=" << g_last_stride.load(std::memory_order_relaxed)
        << "  fmt=" << g_last_format.load(std::memory_order_relaxed);

    out << "\nGE lists=" << g_ge_lists.load(std::memory_order_relaxed)
        << "  last=" << hex32(g_last_ge_list.load(std::memory_order_relaxed));

    out << "\npresent=" << g_present_attempts.load(std::memory_order_relaxed)
        << "  buf=" << hex32(g_last_present_buffer.load(std::memory_order_relaxed))
        << "  " << g_last_present_width.load(std::memory_order_relaxed)
        << "x" << g_last_present_height.load(std::memory_order_relaxed)
        << "  nonBlack=" << non_black << "/" << total;

    out << "\nNATIVE=" << (g_egl_ready.load(std::memory_order_relaxed) ? "OK" : "NO")
        << "  swaps=" << g_swaps.load(std::memory_order_relaxed)
        << "  swapFail=" << g_swap_failures.load(std::memory_order_relaxed)
        << "  nativeFail=" << g_egl_failures.load(std::memory_order_relaxed);

    if (stage == Stage::Finished)
        out << "\nresult=" << g_result.load(std::memory_order_relaxed);

    return out.str();
}

}  // namespace lcs::android_debug
