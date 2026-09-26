#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace lcs {
// Explicit opt-in. 0 idle, 1 requested, 2 recording, 3 ready, -1 error.
bool render_capture_request(std::string path) noexcept;
int render_capture_status() noexcept;
bool render_capture_active() noexcept;
bool render_capture_has_draws() noexcept;
void render_capture_frame_boundary() noexcept;
void render_capture_finish() noexcept;
void render_capture_incomplete() noexcept;
void render_capture_bytes(std::string_view, std::span<const std::byte>) noexcept;
void render_capture_text(std::string_view, std::string_view) noexcept;
std::uint32_t render_capture_next_draw() noexcept;
}
