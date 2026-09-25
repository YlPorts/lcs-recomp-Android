#include "display_window.hpp"

#include "android_host.hpp"
#include "ge_gpu_backend.hpp"
#include "lcs_controls.hpp"
#include "lcs_render_config.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
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

EGLDisplay g_egl_display{EGL_NO_DISPLAY};
EGLSurface g_egl_surface{EGL_NO_SURFACE};
EGLContext g_egl_context{EGL_NO_CONTEXT};
GLuint g_program{};
GLuint g_texture{};
GLuint g_vao{};
GLint g_sampler_location{-1};
std::uint32_t g_texture_width{};
std::uint32_t g_texture_height{};
std::vector<std::uint8_t> g_rgba;

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

GLuint compile_shader(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;

    char buffer[1024]{};
    GLsizei length = 0;
    glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(buffer)), &length, buffer);
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "shader compile failed: %.*s",
                        static_cast<int>(length), buffer);
    glDeleteShader(shader);
    return 0u;
}

void destroy_egl_locked() noexcept {
    if (g_egl_display != EGL_NO_DISPLAY && g_egl_context != EGL_NO_CONTEXT) {
        (void)eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, g_egl_context);
        if (g_texture != 0u) glDeleteTextures(1, &g_texture);
        if (g_vao != 0u) glDeleteVertexArrays(1, &g_vao);
        if (g_program != 0u) glDeleteProgram(g_program);
    }
    g_texture = 0u;
    g_vao = 0u;
    g_program = 0u;
    g_sampler_location = -1;
    g_texture_width = 0u;
    g_texture_height = 0u;

    if (g_egl_display != EGL_NO_DISPLAY) {
        (void)eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_egl_surface != EGL_NO_SURFACE)
            (void)eglDestroySurface(g_egl_display, g_egl_surface);
        if (g_egl_context != EGL_NO_CONTEXT)
            (void)eglDestroyContext(g_egl_display, g_egl_context);
        (void)eglTerminate(g_egl_display);
    }
    g_egl_surface = EGL_NO_SURFACE;
    g_egl_context = EGL_NO_CONTEXT;
    g_egl_display = EGL_NO_DISPLAY;
}

void apply_pending_surface_locked() noexcept {
    if (g_applied_generation == g_surface_generation) return;

    destroy_egl_locked();
    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    g_window = g_pending_window;
    g_pending_window = nullptr;
    g_applied_generation = g_surface_generation;
}

bool create_program_locked() {
    static constexpr char kVertexShader[] = R"(
        #version 300 es
        precision mediump float;
        out vec2 v_uv;
        void main() {
            vec2 p;
            if (gl_VertexID == 0) p = vec2(-1.0, -1.0);
            else if (gl_VertexID == 1) p = vec2(3.0, -1.0);
            else p = vec2(-1.0, 3.0);
            gl_Position = vec4(p, 0.0, 1.0);
            vec2 uv = (p + 1.0) * 0.5;
            v_uv = vec2(uv.x, 1.0 - uv.y);
        }
    )";
    static constexpr char kFragmentShader[] = R"(
        #version 300 es
        precision mediump float;
        uniform sampler2D u_texture;
        in vec2 v_uv;
        out vec4 out_color;
        void main() {
            out_color = texture(u_texture, v_uv);
        }
    )";

    const GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs == 0u || fs == 0u) {
        if (vs != 0u) glDeleteShader(vs);
        if (fs != 0u) glDeleteShader(fs);
        return false;
    }

    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char buffer[1024]{};
        GLsizei length = 0;
        glGetProgramInfoLog(g_program, static_cast<GLsizei>(sizeof(buffer)), &length, buffer);
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "program link failed: %.*s",
                            static_cast<int>(length), buffer);
        glDeleteProgram(g_program);
        g_program = 0u;
        return false;
    }

    g_sampler_location = glGetUniformLocation(g_program, "u_texture");
    glGenVertexArrays(1, &g_vao);
    glGenTextures(1, &g_texture);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return g_vao != 0u && g_texture != 0u;
}

bool ensure_egl_locked() {
    apply_pending_surface_locked();
    if (g_window == nullptr) return false;
    if (g_egl_display != EGL_NO_DISPLAY && g_egl_surface != EGL_NO_SURFACE &&
        g_egl_context != EGL_NO_CONTEXT)
        return true;

    g_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_egl_display == EGL_NO_DISPLAY || !eglInitialize(g_egl_display, nullptr, nullptr)) {
        log_error("eglInitialize failed");
        destroy_egl_locked();
        return false;
    }

    static constexpr EGLint kConfigAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config{};
    EGLint config_count = 0;
    if (!eglChooseConfig(g_egl_display, kConfigAttributes, &config, 1, &config_count) ||
        config_count == 0) {
        log_error("eglChooseConfig failed");
        destroy_egl_locked();
        return false;
    }

    EGLint native_format = 0;
    (void)eglGetConfigAttrib(g_egl_display, config, EGL_NATIVE_VISUAL_ID, &native_format);
    (void)ANativeWindow_setBuffersGeometry(g_window, 0, 0, native_format);

    static constexpr EGLint kContextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    g_egl_context = eglCreateContext(
        g_egl_display, config, EGL_NO_CONTEXT, kContextAttributes);
    if (g_egl_context == EGL_NO_CONTEXT) {
        log_error("eglCreateContext ES3 failed");
        destroy_egl_locked();
        return false;
    }

    g_egl_surface = eglCreateWindowSurface(g_egl_display, config, g_window, nullptr);
    if (g_egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, g_egl_context)) {
        log_error("eglCreateWindowSurface/eglMakeCurrent failed");
        destroy_egl_locked();
        return false;
    }

    (void)eglSwapInterval(g_egl_display, 1);
    if (!create_program_locked()) {
        destroy_egl_locked();
        return false;
    }
    return true;
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
    case 0u: {
        const std::uint16_t value =
            static_cast<std::uint16_t>(source[0] | (source[1] << 8u));
        r = (value & 0x1Fu) * 255u / 31u;
        g = ((value >> 5u) & 0x3Fu) * 255u / 63u;
        b = ((value >> 11u) & 0x1Fu) * 255u / 31u;
        break;
    }
    case 1u: {
        const std::uint16_t value =
            static_cast<std::uint16_t>(source[0] | (source[1] << 8u));
        r = (value & 0x1Fu) * 255u / 31u;
        g = ((value >> 5u) & 0x1Fu) * 255u / 31u;
        b = ((value >> 10u) & 0x1Fu) * 255u / 31u;
        a = (value & 0x8000u) != 0u ? 255u : 0u;
        break;
    }
    case 2u: {
        const std::uint16_t value =
            static_cast<std::uint16_t>(source[0] | (source[1] << 8u));
        r = (value & 0xFu) * 17u;
        g = ((value >> 4u) & 0xFu) * 17u;
        b = ((value >> 8u) & 0xFu) * 17u;
        a = ((value >> 12u) & 0xFu) * 17u;
        break;
    }
    default:
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

void present_rgba_locked(std::span<const std::byte> rgba,
                         std::uint32_t width, std::uint32_t height) {
    if (width == 0u || height == 0u ||
        rgba.size() < static_cast<std::size_t>(width) * height * 4u ||
        !ensure_egl_locked())
        return;

    EGLint surface_width = 0;
    EGLint surface_height = 0;
    if (!eglQuerySurface(g_egl_display, g_egl_surface, EGL_WIDTH, &surface_width) ||
        !eglQuerySurface(g_egl_display, g_egl_surface, EGL_HEIGHT, &surface_height) ||
        surface_width <= 0 || surface_height <= 0)
        return;

    const LcsConfiguration &config = lcs_render_configuration();
    const PresentationRectangle rect = calculate_presentation_rectangle(
        static_cast<std::uint32_t>(surface_width),
        static_cast<std::uint32_t>(surface_height),
        width, height, config.display.aspect_mode, config.display.integer_scale);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, surface_width, surface_height);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(rect.x, rect.y, rect.width, rect.height);
    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_texture);

    const GLint filter = config.display.upscale_filter == DisplayUpscaleFilter::Nearest
        ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const auto *pixels = reinterpret_cast<const std::uint8_t *>(rgba.data());
    if (g_texture_width != width || g_texture_height != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        g_texture_width = width;
        g_texture_height = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    if (g_sampler_location >= 0) glUniform1i(g_sampler_location, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    (void)eglSwapBuffers(g_egl_display, g_egl_surface);
}

}  // namespace

namespace android_host {

void set_window(ANativeWindow *window) noexcept {
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    if (g_pending_window != nullptr) ANativeWindow_release(g_pending_window);
    g_pending_window = window;
    ++g_surface_generation;
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

void display_window_attach_gpu_backend() {}

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

    if (frame_buffer == 0u || width == 0u || height == 0u || buffer_width == 0u) return;
    const std::uint32_t bpp = bytes_per_pixel(pixel_format);
    const std::uint32_t stride = buffer_width * bpp;
    const std::size_t total = static_cast<std::size_t>(stride) * height;
    const std::uint8_t *source = runtime.memory().raw_pointer(frame_buffer, total);
    if (source == nullptr) return;

    g_rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    for (std::uint32_t y = 0u; y < height; ++y) {
        const std::uint8_t *row = source + static_cast<std::size_t>(y) * stride;
        for (std::uint32_t x = 0u; x < width; ++x) {
            unpack_rgba(row + static_cast<std::size_t>(x) * bpp, pixel_format,
                        g_rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4u);
        }
    }

    const auto bytes = std::as_bytes(std::span<const std::uint8_t>(g_rgba));
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    present_rgba_locked(bytes, width, height);
}

void display_window_present_rgba(std::span<const std::byte> rgba,
                                 std::uint32_t width, std::uint32_t height) {
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    present_rgba_locked(rgba, width, height);
}

void display_window_shutdown() {
    std::lock_guard<std::mutex> guard(g_surface_mutex);
    destroy_egl_locked();
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
    input.camera_x = std::clamp(g_camera_x.load(std::memory_order_relaxed), -127, 127);
    input.camera_y = std::clamp(g_camera_y.load(std::memory_order_relaxed), -127, 127);
    input.accelerate = g_accelerate.load(std::memory_order_relaxed);
    input.brake = g_brake.load(std::memory_order_relaxed);

    if (lcs_player_in_vehicle()) {
        if ((input.buttons & (kPspCross | kPspRTrigger)) != 0u) input.accelerate = true;
        if ((input.buttons & (kPspSquare | kPspLTrigger)) != 0u) input.brake = true;
    }
    lcs_camera_set_axes(input.camera_x, input.camera_y);
    lcs_set_host_drive_inputs(input.accelerate, input.brake);
    return input;
}

}  // namespace lcs
