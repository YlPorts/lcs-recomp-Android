#pragma once

#include <android/native_window.h>
#include <cstdint>

namespace lcs::android_host {

void set_window(ANativeWindow *window) noexcept;
void set_display_size(std::uint32_t width, std::uint32_t height) noexcept;
void set_source_size(std::uint32_t width, std::uint32_t height) noexcept;

[[nodiscard]] std::uint32_t display_width() noexcept;
[[nodiscard]] std::uint32_t display_height() noexcept;
[[nodiscard]] std::uint32_t source_width() noexcept;
[[nodiscard]] std::uint32_t source_height() noexcept;

// Screen-space HUD correction for Android's stretched low-resolution buffer.
[[nodiscard]] float ultrawide_x_scale() noexcept;

// Three-dimensional geometry normally needs no late correction: the guest
// camera/frustum hooks already use the physical display aspect. Retain the
// legacy fallback only when those hooks are disabled or not initialized.
[[nodiscard]] float geometry_x_scale() noexcept;

void set_input(std::uint32_t buttons, std::uint8_t analog_x, std::uint8_t analog_y,
               int camera_x, int camera_y, bool accelerate, bool brake) noexcept;
void request_stop() noexcept;
bool stop_requested() noexcept;

}  // namespace lcs::android_host
