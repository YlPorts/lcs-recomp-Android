#include "display_window.hpp"

#include "android_host.hpp"
#include "android_debug.hpp"
#include "ge_gpu_backend.hpp"
#include "lcs_controls.hpp"
#include "lcs_render_config.hpp"

#include <android/log.h>
#include <android/native_window.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <vector>

namespace lcs {
namespace {

constexpr const char *kLogTag = "LCSRecomp";
constexpr std::uint32_t kPspCross = 0x004000u;
constexpr std::uint32_t kPspSquare = 0x008000u;
constexpr std::uint32_t kPspLTrigger = 0x000100u;
constexpr std::uint32_t kPspRTrigger = 0x000200u;

std::mutex g_surface_mutex;
ANativeWindow *g_window{};
ANativeWindow *g_pending_window{};
std::uint64_t g_surface_generation{};
std::uint64_t g_applied_generation{};
std::uint32_t g_buffer_geometry_width{};
std::uint32_t g_buffer_geometry_height{};
std::vector<std::uint8_t> g_rgba;

std::atomic<std::uint32_t> g_display_width{0u};
std::atomic<std::uint32_t> g_display_height{0u};
std::atomic<std::uint32_t> g_source_width{480u};
std::atomic<std::uint32_t> g_source_height{272u};

std::atomic<std::uint32_t> g_buttons{0u};
std::atomic<int> g_analog_x{128};
std::atomic<int> g_analog_y{128};
std::atomic<int> g_camera_x{0};
std::atomic<int> g_camera_y{0};
std::atomic<bool> g_accelerate{false};
std::atomic<bool> g_brake{false};
std::atomic<bool> g_stop_requested{false};

void log_error(const char *message) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

void apply_pending_surface_locked() noexcept {
    if (g_applied_generation == g_surface_generation) return;

    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    g_window = g_pending_window;
    g_pending_window = nullptr;
    g_applied_generation = g_surface_generation;
    g_buffer_geometry_width = 0u;
    g_buffer_geometry_height = 0u;
}

std::uint32_t bytes_per_pixel(std::uint32_t format) noexcept {
    return format == 3u ? 4u : 2u;
}

void unpack_rgba(const std::uint8_t *source, std::uint32_t format,
                 std::uint8_t *destination) noexcept {
    std::uint32_t r = 0u;
    std::uint32_t g = 0u;
    std::uint32_t b = 0u;
    std::uint32_t a = 255u;
    switch (format) {
    case 0u: {  // GU_PSM_5650
        const std::uint16_t value =
            static_cast<std::uint16_t>(source[0] | (source[1] << 8u));
        r = (value & 0x1Fu) * 255u / 31u;
        g = ((value >> 5u) & 0x3Fu) * 255u / 63u;
        b = ((value >> 11u) & 0x1Fu) * 255u / 31u;
        break;
    }
    case 1u: {  // GU_PSM_5551
        const std::uint16_t value =
            static_cast<std::uint16_t>(source[0] | (source[1] << 8u));
        r = (value & 0x1Fu) * 255u / 31u;
        g = ((value >> 5u) & 0x1Fu) * 255u / 31u;
        b = ((value >> 10u) & 0x1Fu) * 255u / 31u;
        a = (value & 0x8000u) != 0u ? 255u : 0u;
        break;
    }
    case 2u: {  // GU_PSM_4444
        const std::uint16_t value =
            static_cast<std::uint16_t>(source[0] | (source[1] << 8u));
        r = (value & 0xFu) * 17u;
        g = ((value >> 4u) & 0xFu) * 17u;
        b = ((value >> 8u) & 0xFu) * 17u;
        a = ((value >> 12u) & 0xFu) * 17u;
        break;
    }
    default:  // GU_PSM_8888
        r = source[0];
        g = source[1];
        b = source[2];
        a = source[3];
        break;
    }
    destination[0] = static_cast<std::uint8_t>(r);
    destination[1] = static_cast<std::uint8_t>(g);
    destination[2] = static_cast<std::uint8_t>(b);
    destination[3] = static_cast<std::uint8_t>(a);
}

void present_rgba_rows_locked(const std::uint8_t *source,
                              std::size_t source_stride_bytes,
                              std::uint32_t width, std::uint32_t height) {
    apply_pending_surface_locked();
    if (g_window == nullptr || source == nullptr || width == 0u || height == 0u) {
        android_debug::note_egl_ready(false);
        return;
    }

    android_host::set_source_size(width, height);

    if (g_buffer_geometry_width != width || g_buffer_geometry_height != height) {
        if (ANativeWindow_setBuffersGeometry(
                g_window, static_cast<int32_t>(width), static_cast<int32_t>(height),
                WINDOW_FORMAT_RGBA_8888) != 0) {
            android_debug::note_egl_ready(false);
            log_error("ANativeWindow_setBuffersGeometry(native) failed");
            return;
        }
        g_buffer_geometry_width = width;
        g_buffer_geometry_height = height;
    }

    ANativeWindow_Buffer buffer{};
    if (ANativeWindow_lock(g_window, &buffer, nullptr) != 0 ||
        buffer.bits == nullptr || buffer.width <= 0 || buffer.height <= 0 ||
        buffer.stride <= 0) {
        android_debug::note_egl_ready(false);
        log_error("ANativeWindow_lock failed");
        return;
    }

    android_debug::note_egl_ready(true);

    auto *destination = static_cast<std::uint8_t *>(buffer.bits);
    const std::size_t destination_row_bytes =
        static_cast<std::size_t>(buffer.stride) * 4u;
    const std::uint32_t copy_width = std::min<std::uint32_t>(
        width, static_cast<std::uint32_t>(buffer.width));
    const std::uint32_t copy_height = std::min<std::uint32_t>(
        height, static_cast<std::uint32_t>(buffer.height));
    const std::size_t copy_bytes = static_cast<std::size_t>(copy_width) * 4u;

    for (std::uint32_t y = 0u; y < copy_height; ++y) {
        std::uint8_t *dst_row =
            destination + static_cast<std::size_t>(y) * destination_row_bytes;
        const std::uint8_t *src_row =
            source + static_cast<std::size_t>(y) * source_stride_bytes;
        std::memcpy(dst_row, src_row, copy_bytes);
        if (destination_row_bytes > copy_bytes)
            std::memset(dst_row + copy_bytes, 0, destination_row_bytes - copy_bytes);
    }
    for (std::uint32_t y = copy_height;
         y < static_cast<std::uint32_t>(buffer.height); ++y) {
        std::memset(destination + static_cast<std::size_t>(y) * destination_row_bytes,
                    0, destination_row_bytes);
    }

    const bool posted = ANativeWindow_unlockAndPost(g_window) == 0;
    android_debug::note_swap(posted);
    if (!posted) log_error("ANativeWindow_unlockAndPost failed");
}

void present_rgba_locked(std::span<const std::byte> rgba,
                         std::uint32_t width, std::uint32_t height) {
    if (rgba.size() < static_cast<std::size_t>(width) * height * 4u) return;
    present_rgba_rows_locked(
        reinterpret_cast<const std::uint8_t *>(rgba.data()),
        static_cast<std::size_t>(width) * 4u, width, height);
}

}  // namespace

namespace android_host {

void set_window(ANativeWindow *window) noexcept {
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    if (g_pending_window != nullptr) ANativeWindow_release(g_pending_window);
    g_pending_window = window;
    ++g_surface_generation;
}

void set_display_size(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0u || height == 0u) return;
    g_display_width.store(width, std::memory_order_relaxed);
    g_display_height.store(height, std::memory_order_relaxed);
}

void set_source_size(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0u || height == 0u) return;
    g_source_width.store(width, std::memory_order_relaxed);
    g_source_height.store(height, std::memory_order_relaxed);
}

std::uint32_t display_width() noexcept {
    return g_display_width.load(std::memory_order_relaxed);
}

std::uint32_t display_height() noexcept {
    return g_display_height.load(std::memory_order_relaxed);
}

std::uint32_t source_width() noexcept {
    return g_source_width.load(std::memory_order_relaxed);
}

std::uint32_t source_height() noexcept {
    return g_source_height.load(std::memory_order_relaxed);
}

float ultrawide_x_scale() noexcept {
    const std::uint32_t dw = display_width();
    const std::uint32_t dh = display_height();
    const std::uint32_t sw = source_width();
    const std::uint32_t sh = source_height();
    if (dw == 0u || dh == 0u || sw == 0u || sh == 0u) return 1.0f;
    const float display_aspect = static_cast<float>(dw) / static_cast<float>(dh);
    const float source_aspect = static_cast<float>(sw) / static_cast<float>(sh);
    if (!(display_aspect > 0.0f) || !(source_aspect > 0.0f)) return 1.0f;
    return std::clamp(source_aspect / display_aspect, 0.50f, 1.50f);
}

void set_input(std::uint32_t buttons, std::uint8_t analog_x, std::uint8_t analog_y,
               int camera_x, int camera_y, bool accelerate, bool brake) noexcept {
    g_buttons.store(buttons, std::memory_order_relaxed);
    g_analog_x.store(analog_x, std::memory_order_relaxed);
    g_analog_y.store(analog_y, std::memory_order_relaxed);
    g_camera_x.store(std::clamp(camera_x, -127, 127), std::memory_order_relaxed);
    g_camera_y.store(std::clamp(camera_y, -127, 127), std::memory_order_relaxed);
    g_accelerate.store(accelerate, std::memory_order_relaxed);
    g_brake.store(brake, std::memory_order_relaxed);
}

void request_stop() noexcept {
    g_stop_requested.store(true, std::memory_order_relaxed);
}

bool stop_requested() noexcept {
    return g_stop_requested.load(std::memory_order_relaxed);
}

}  // namespace android_host

void display_window_init() {
    g_stop_requested.store(false, std::memory_order_relaxed);
}

void display_window_attach_gpu_backend() {
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    apply_pending_surface_locked();
    ge_gpu_backend_set_native_window(g_window);
}

bool display_window_profile_key_pressed() { return false; }

bool display_window_closed() {
    return android_host::stop_requested();
}

void display_window_pump() {}

void display_window_present(psprecomp::Runtime &runtime, std::uint32_t frame_buffer,
                            std::uint32_t buffer_width, std::uint32_t pixel_format,
                            std::uint32_t width, std::uint32_t height) {
    static auto last_present = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_present < std::chrono::milliseconds(33)) return;
    last_present = now;

    android_host::set_source_size(width, height);
    android_debug::note_present_attempt(
        frame_buffer, buffer_width, pixel_format, width, height);

    if (frame_buffer == 0u || width == 0u || height == 0u ||
        buffer_width == 0u || pixel_format > 3u) {
        android_debug::note_present_pixels(0u, 0u);
        return;
    }

    const std::uint32_t bpp = bytes_per_pixel(pixel_format);
    const std::uint32_t stride_bytes = buffer_width * bpp;
    const std::size_t total_bytes =
        static_cast<std::size_t>(stride_bytes) * height;
    const std::uint8_t *source =
        runtime.memory().raw_pointer(frame_buffer, total_bytes);
    if (source == nullptr) {
        android_debug::note_present_pixels(0u, 0u);
        return;
    }

    // LCS uses 32-bit GU_PSM_8888 for its main color target. Fast-path that
    // overwhelmingly common case directly from guest VRAM into the native
    // buffer: no per-pixel conversion and no intermediate full-frame copy.
    if (pixel_format == 3u) {
        android_debug::note_present_pixels(
            1u, static_cast<std::uint64_t>(width) * height);
        std::lock_guard<std::mutex> guard(g_surface_mutex);
        present_rgba_rows_locked(source, stride_bytes, width, height);
        return;
    }

    g_rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    for (std::uint32_t y = 0u; y < height; ++y) {
        const std::uint8_t *row =
            source + static_cast<std::size_t>(y) * stride_bytes;
        for (std::uint32_t x = 0u; x < width; ++x) {
            std::uint8_t *pixel =
                g_rgba.data() +
                (static_cast<std::size_t>(y) * width + x) * 4u;
            unpack_rgba(row + static_cast<std::size_t>(x) * bpp,
                        pixel_format, pixel);
        }
    }

    const auto bytes =
        std::as_bytes(std::span<const std::uint8_t>(g_rgba));
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    present_rgba_locked(bytes, width, height);
}

void display_window_present_rgba(std::span<const std::byte> rgba,
                                 std::uint32_t width, std::uint32_t height) {
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    present_rgba_locked(rgba, width, height);
}

void display_window_shutdown() {
    ge_gpu_backend_set_native_window(nullptr);
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    if (g_pending_window != nullptr) {
        ANativeWindow_release(g_pending_window);
        g_pending_window = nullptr;
    }
}

HostInputState display_window_input() {
    HostInputState input{};
    input.buttons = g_buttons.load(std::memory_order_relaxed);
    input.analog_x = static_cast<std::uint8_t>(
        std::clamp(g_analog_x.load(std::memory_order_relaxed), 0, 255));
    input.analog_y = static_cast<std::uint8_t>(
        std::clamp(g_analog_y.load(std::memory_order_relaxed), 0, 255));
    input.camera_x =
        std::clamp(g_camera_x.load(std::memory_order_relaxed), -127, 127);
    input.camera_y =
        std::clamp(g_camera_y.load(std::memory_order_relaxed), -127, 127);
    input.accelerate = g_accelerate.load(std::memory_order_relaxed);
    input.brake = g_brake.load(std::memory_order_relaxed);

    if (lcs_player_in_vehicle()) {
        if ((input.buttons & (kPspCross | kPspRTrigger)) != 0u)
            input.accelerate = true;
        if ((input.buttons & (kPspSquare | kPspLTrigger)) != 0u)
            input.brake = true;
    }

    lcs_camera_set_axes(input.camera_x, input.camera_y);
    lcs_set_host_drive_inputs(input.accelerate, input.brake);
    return input;
}

}  // namespace lcs
