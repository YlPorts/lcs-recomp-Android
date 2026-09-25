#pragma once

#include <android/native_window.h>
#include <cstdint>

namespace lcs::android_host {

void set_window(ANativeWindow *window) noexcept;
void set_input(std::uint32_t buttons, std::uint8_t analog_x, std::uint8_t analog_y,
               int camera_x, int camera_y, bool accelerate, bool brake) noexcept;
void request_stop() noexcept;
bool stop_requested() noexcept;

}  // namespace lcs::android_host
