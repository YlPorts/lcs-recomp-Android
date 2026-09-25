#pragma once

#include <cstdint>
#include <string>

namespace lcs::android_debug {

enum class Stage : std::uint32_t {
    Idle = 0,
    NativeEntry,
    MainStarted,
    ElfLoaded,
    RuntimeReady,
    ProfileInstalled,
    Running,
    Finished,
};

void reset() noexcept;
void set_stage(Stage stage) noexcept;
void set_result(int result) noexcept;

void note_set_framebuffer(std::uint32_t address, std::uint32_t stride,
                          std::uint32_t format) noexcept;
void note_vblank(std::uint64_t index, std::uint32_t framebuffer) noexcept;
void note_ge_list(std::uint32_t address) noexcept;

void note_present_attempt(std::uint32_t buffer, std::uint32_t stride,
                          std::uint32_t format, std::uint32_t width,
                          std::uint32_t height) noexcept;
void note_present_pixels(std::uint64_t non_black, std::uint64_t total) noexcept;
void note_egl_ready(bool ready) noexcept;
void note_swap(bool ok) noexcept;

[[nodiscard]] std::string status_text(std::uint32_t pc, std::int32_t thread_uid,
                                      const char *thread_name);

}  // namespace lcs::android_debug
