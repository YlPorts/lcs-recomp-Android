#include "ge_gpu_backend.hpp"

#if defined(__ANDROID__)

#include "lcs_render_config.hpp"
#include "lcs_runtime_log.hpp"
#include "android_host.hpp"
#include "lcs_android_gpu_policy.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lcs {
namespace {

constexpr std::uint32_t kReferenceWidth = 480u;
constexpr std::uint32_t kReferenceHeight = 272u;

struct GlesTexture {
    GLuint id{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t levels{1u};
    std::uint64_t signature{};
    std::uint64_t signature_epoch{};
    std::uint64_t last_used_epoch{};
    std::uint64_t byte_size{};
};

struct GlesTarget {
    std::uint32_t address{};
    std::uint32_t logical_width{kReferenceWidth};
    std::uint32_t logical_height{kReferenceHeight};
    std::uint32_t render_width{};
    std::uint32_t render_height{};
    GLuint color{};
    GLuint depth{};
    GLuint fbo{};
    GLuint feedback{};
    bool cleared{};
};

struct GlesBatch {
    GeGpuDrawDescriptor draw{};
    std::vector<GeGpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t first_vertex{};
    std::uint32_t first_index{};
    bool hardware_transform{};
    GeGpuHardwareTransform transform{};
};

struct GlesState {
    bool initialized{};
    bool enabled{};
    bool gl_ready{};
    bool direct_present_ok{};
    std::thread::id gl_thread{};
    std::uint32_t scale{2u};
    std::uint64_t frame_epoch{1u};
    std::uint64_t perf_frame_ns{};
    std::uint64_t perf_frame_count{};
    std::chrono::steady_clock::time_point perf_window{};
    std::uint64_t perf_presents{};
    std::uint64_t perf_epochs{};
    std::uint64_t perf_color_tests{};

    std::mutex window_mutex;
    ANativeWindow *window{};
    std::atomic<bool> surface_dirty{true};

    EGLDisplay display{EGL_NO_DISPLAY};
    EGLConfig config{};
    EGLContext context{EGL_NO_CONTEXT};
    EGLSurface surface{EGL_NO_SURFACE};

    GLuint program{};
    GLuint present_program{};
    GLuint vao{};
    GLuint vbo{};
    GLuint ebo{};
    GLuint present_vao{};

    GLint u_mode{-1};
    GLint u_logical_size{-1};
    GLint u_row0{-1};
    GLint u_row1{-1};
    GLint u_row2{-1};
    GLint u_row3{-1};
    GLint u_view_z{-1};
    GLint u_uv{-1};
    GLint u_fog_params{-1};
    GLint u_vertex_color_mul{-1};
    GLint u_vertex_color_add{-1};
    GLint u_vertex_color_affine{-1};

    GLint u_tex{-1};
    GLint u_texture_flip_v{-1};
    GLint u_color_test{-1};
    GLint u_color_reference{-1};
    GLint u_color_mask{-1};
    GLint u_texture_enabled{-1};
    GLint u_texture_function{-1};
    GLint u_texture_use_alpha{-1};
    GLint u_texture_double{-1};
    GLint u_texture_env{-1};
    GLint u_alpha_enabled{-1};
    GLint u_alpha_func{-1};
    GLint u_alpha_ref{-1};
    GLint u_alpha_mask{-1};
    GLint u_fog_enabled{-1};
    GLint u_fog_color{-1};
    GLint u_framebuffer_format{-1};

    GLint u_present_tex{-1};

    std::unordered_map<std::uint64_t, GlesTexture> textures;
    android_detail::TextureVersions texture_versions;
    std::vector<GeGpuVertex> frame_vertices;
    std::vector<std::uint32_t> frame_indices;
    std::uint64_t texture_cache_bytes{};
    std::uint32_t texture_cache_entry_limit{8192u};
    std::uint64_t texture_cache_byte_limit{256ull * 1024ull * 1024ull};
    std::unordered_map<std::uint32_t, GlesTarget> targets;
    std::vector<GlesBatch> batches;
    std::vector<std::byte> last_texture_rgba;

    std::uint32_t display_framebuffer{};
    std::uint32_t display_logical_width{kReferenceWidth};
    std::uint32_t display_logical_height{kReferenceHeight};
    std::uint32_t presented_framebuffer{};

    GeGpuBackendReport report{};
};

GlesState &state() {
    static GlesState s;
    return s;
}

std::uint32_t read_scale() noexcept {
    const char *text = std::getenv("PSPRECOMP_GLES_SCALE");
    if (text == nullptr || *text == '\0') return 2u;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') return 2u;
    return static_cast<std::uint32_t>(std::clamp<unsigned long>(value, 1u, 4u));
}

std::string egl_error(const char *where) {
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%s (EGL=0x%04X)", where,
                  static_cast<unsigned>(eglGetError()));
    return buffer;
}

std::string gl_error(const char *where) {
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%s (GL=0x%04X)", where,
                  static_cast<unsigned>(glGetError()));
    return buffer;
}

GLuint compile_shader(GLenum type, const char *source, std::string &error) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0u) {
        error = gl_error("glCreateShader");
        return 0u;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &written, log.data());
    if (written >= 0 && static_cast<std::size_t>(written) < log.size())
        log.resize(static_cast<std::size_t>(written));
    error = log;
    glDeleteShader(shader);
    return 0u;
}

GLuint link_program(const char *vs_source, const char *fs_source, std::string &error) {
    const GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_source, error);
    if (vs == 0u) return 0u;
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source, error);
    if (fs == 0u) {
        glDeleteShader(vs);
        return 0u;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return program;

    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &written, log.data());
    if (written >= 0 && static_cast<std::size_t>(written) < log.size())
        log.resize(static_cast<std::size_t>(written));
    error = log;
    glDeleteProgram(program);
    return 0u;
}

void delete_target(GlesTarget &target) noexcept {
    if (target.feedback != 0u) glDeleteTextures(1, &target.feedback);
    if (target.fbo != 0u) glDeleteFramebuffers(1, &target.fbo);
    if (target.depth != 0u) glDeleteRenderbuffers(1, &target.depth);
    if (target.color != 0u) glDeleteTextures(1, &target.color);
    target.feedback = 0u;
    target.fbo = 0u;
    target.depth = 0u;
    target.color = 0u;
    target.render_width = 0u;
    target.render_height = 0u;
    target.cleared = false;
}

void destroy_gl_objects(GlesState &s) noexcept {
    if (!s.gl_ready) return;
    for (auto &[key, texture] : s.textures) {
        (void)key;
        if (texture.id != 0u) glDeleteTextures(1, &texture.id);
    }
    s.textures.clear();
    s.texture_versions.clear();
    s.frame_vertices.clear();
    s.frame_indices.clear();
    s.texture_cache_bytes = 0u;
    for (auto &[key, target] : s.targets) {
        (void)key;
        delete_target(target);
    }
    s.targets.clear();
    if (s.vbo != 0u) glDeleteBuffers(1, &s.vbo);
    if (s.ebo != 0u) glDeleteBuffers(1, &s.ebo);
    if (s.vao != 0u) glDeleteVertexArrays(1, &s.vao);
    if (s.present_vao != 0u) glDeleteVertexArrays(1, &s.present_vao);
    if (s.program != 0u) glDeleteProgram(s.program);
    if (s.present_program != 0u) glDeleteProgram(s.present_program);
    s.vbo = s.ebo = s.vao = s.present_vao = 0u;
    s.program = s.present_program = 0u;
    s.gl_ready = false;
}

bool create_gl_objects(GlesState &s, std::string &error) {
    runtime_log_line("gles: create_gl_objects begin");
    static constexpr char kVertexShader[] = R"GLSL(#version 300 es
precision highp float;
precision highp int;

layout(location=0) in vec4 aPosition;
layout(location=1) in vec4 aColor;
layout(location=2) in vec2 aUv;
layout(location=3) in float aFogFactor;
layout(location=4) in float aQ;

uniform int uMode;
uniform vec2 uLogicalSize;
uniform vec4 uRow0;
uniform vec4 uRow1;
uniform vec4 uRow2;
uniform vec4 uRow3;
uniform vec4 uViewZ;
uniform vec4 uUvScaleOffset;
uniform vec2 uFogParams;
uniform vec4 uVertexColorMul;
uniform vec4 uVertexColorAdd;
uniform int uVertexColorAffine;

out vec4 vColor;
out vec2 vUv;
out float vFogFactor;
out float vQ;

void main() {
    if (uMode == 1) {
        vec4 p = aPosition;
        float clipW = dot(uRow3, p);
        if (abs(clipW) < 1.0e-12) clipW = 1.0;
        float z01w = dot(uRow2, p);
        float zClip = z01w * 2.0 - clipW;
        gl_Position = vec4(dot(uRow0, p), dot(uRow1, p), zClip, clipW);
        vUv = aUv * uUvScaleOffset.xy + uUvScaleOffset.zw;
        float viewZ = dot(uViewZ, p);
        vFogFactor = clamp((viewZ + uFogParams.x) * uFogParams.y, 0.0, 1.0);
    } else {
        float w = abs(aPosition.w) < 1.0e-12 ? 1.0 : aPosition.w;
        float sx = 2.0 / max(uLogicalSize.x, 1.0);
        float sy = 2.0 / max(uLogicalSize.y, 1.0);
        float z01 = clamp(aPosition.z * (1.0 / 65535.0), 0.0, 1.0);
        gl_Position = vec4(
            (aPosition.x * sx - 1.0) * w,
            (1.0 - aPosition.y * sy) * w,
            (z01 * 2.0 - 1.0) * w,
            w);
        vUv = aUv;
        vFogFactor = aFogFactor;
    }

    vColor = aColor;
    if (uVertexColorAffine != 0)
        vColor = floor(clamp(vColor * uVertexColorMul + uVertexColorAdd, 0.0, 1.0) * 255.0) / 255.0;
    vQ = aQ;
}
)GLSL";

    static constexpr char kFragmentShader[] = R"GLSL(#version 300 es
precision highp float;
precision highp int;

uniform sampler2D uTexture;
uniform int uTextureFlipV;
uniform int uColorTest;
uniform ivec3 uColorReference;
uniform ivec3 uColorMask;
uniform int uTextureEnabled;
uniform int uTextureFunction;
uniform int uTextureUseAlpha;
uniform int uTextureDouble;
uniform vec3 uTextureEnv;

uniform int uAlphaEnabled;
uniform int uAlphaFunc;
uniform int uAlphaRef;
uniform int uAlphaMask;

uniform int uFogEnabled;
uniform vec3 uFogColor;
uniform int uFramebufferFormat;

in vec4 vColor;
in vec2 vUv;
in float vFogFactor;
in float vQ;
out vec4 outColor;

bool alphaPass(int fn, int lhs, int rhs) {
    if (fn == 0) return false;
    if (fn == 1) return true;
    if (fn == 2) return lhs == rhs;
    if (fn == 3) return lhs != rhs;
    if (fn == 4) return lhs < rhs;
    if (fn == 5) return lhs <= rhs;
    if (fn == 6) return lhs > rhs;
    return lhs >= rhs;
}

float quantize(float value, float levels) {
    return floor(clamp(value, 0.0, 1.0) * levels + 0.5) / levels;
}

void main() {
    vec4 color = clamp(vColor, 0.0, 1.0);
    if (uTextureEnabled != 0) {
        float q = abs(vQ) < 1.0e-20 ? 1.0 : vQ;
        vec2 sampleUv = vUv / q;
        if (uTextureFlipV != 0) sampleUv.y = 1.0 - sampleUv.y;
        // Match the D3D12 backend: PSP UV has already been transformed into
        // sampler space by the GE vertex path. Do not renormalize or flip here.
        vec4 texel = texture(uTexture, sampleUv);
        int fn = uTextureFunction & 7;
        if (fn == 0) {
            color.rgb *= texel.rgb;
            if (uTextureUseAlpha != 0) color.a *= texel.a;
        } else if (fn == 1) {
            float a = uTextureUseAlpha != 0 ? texel.a : 1.0;
            color.rgb = mix(color.rgb, texel.rgb, a);
        } else if (fn == 2) {
            color.rgb = mix(color.rgb, uTextureEnv, texel.rgb);
            if (uTextureUseAlpha != 0) color.a *= texel.a;
        } else if (fn == 3) {
            float oldAlpha = color.a;
            color = texel;
            if (uTextureUseAlpha == 0) color.a = oldAlpha;
        } else if (fn == 4) {
            color.rgb = clamp(color.rgb + texel.rgb, 0.0, 1.0);
            if (uTextureUseAlpha != 0) color.a *= texel.a;
        }
        if (uTextureDouble != 0) color.rgb = clamp(color.rgb * 2.0, 0.0, 1.0);
    }

    if (uColorTest >= 0) {
        ivec3 rgb = ivec3(floor(clamp(color.rgb, 0.0, 1.0)*255.0+0.5)) & uColorMask;
        ivec3 reference = uColorReference & uColorMask;
        bool rgbEqual = all(equal(rgb, reference));
        if (uColorTest == 0 || (uColorTest == 2 && !rgbEqual) ||
            (uColorTest == 3 && rgbEqual)) discard;
    }
    if (uFogEnabled != 0)
        color.rgb = mix(uFogColor, color.rgb, clamp(vFogFactor, 0.0, 1.0));

    if (uAlphaEnabled != 0) {
        int a = int(floor(clamp(color.a, 0.0, 1.0) * 255.0 + 0.5));
        if (!alphaPass(uAlphaFunc, a & uAlphaMask, uAlphaRef & uAlphaMask))
            discard;
    }

    if (uFramebufferFormat == 0) {
        color.r = quantize(color.r, 31.0);
        color.g = quantize(color.g, 63.0);
        color.b = quantize(color.b, 31.0);
        color.a = 1.0;
    } else if (uFramebufferFormat == 1) {
        color.rgb = vec3(quantize(color.r,31.0), quantize(color.g,31.0), quantize(color.b,31.0));
        color.a = color.a >= 0.5 ? 1.0 : 0.0;
    } else if (uFramebufferFormat == 2) {
        color = vec4(quantize(color.r,15.0), quantize(color.g,15.0),
                     quantize(color.b,15.0), quantize(color.a,15.0));
    }

    outColor = color;
}
)GLSL";

    static constexpr char kPresentVertex[] = R"GLSL(#version 300 es
precision highp float;
out vec2 vUv;
void main() {
    vec2 p;
    if (gl_VertexID == 0) p = vec2(-1.0, -1.0);
    else if (gl_VertexID == 1) p = vec2(3.0, -1.0);
    else p = vec2(-1.0, 3.0);
    gl_Position = vec4(p, 0.0, 1.0);
    vUv = vec2((p.x + 1.0) * 0.5, (p.y + 1.0) * 0.5);
}
)GLSL";

    static constexpr char kPresentFragment[] = R"GLSL(#version 300 es
precision mediump float;
uniform sampler2D uPresentTexture;
in vec2 vUv;
out vec4 outColor;
void main() {
    outColor = texture(uPresentTexture, vUv);
}
)GLSL";

    s.program = link_program(kVertexShader, kFragmentShader, error);
    if (s.program == 0u) return false;
    s.present_program = link_program(kPresentVertex, kPresentFragment, error);
    if (s.present_program == 0u) return false;

    glGenVertexArrays(1, &s.vao);
    glGenBuffers(1, &s.vbo);
    glGenBuffers(1, &s.ebo);
    glGenVertexArrays(1, &s.present_vao);
    if (s.vao == 0u || s.vbo == 0u || s.ebo == 0u || s.present_vao == 0u) {
        error = gl_error("create GLES buffers");
        return false;
    }

    glBindVertexArray(s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(GeGpuVertex),
                          reinterpret_cast<void *>(offsetof(GeGpuVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GeGpuVertex),
                          reinterpret_cast<void *>(offsetof(GeGpuVertex, rgba)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GeGpuVertex),
                          reinterpret_cast<void *>(offsetof(GeGpuVertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GeGpuVertex),
                          reinterpret_cast<void *>(offsetof(GeGpuVertex, fog_factor)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(GeGpuVertex),
                          reinterpret_cast<void *>(offsetof(GeGpuVertex, q)));
    glBindVertexArray(0);

    s.u_mode = glGetUniformLocation(s.program, "uMode");
    s.u_logical_size = glGetUniformLocation(s.program, "uLogicalSize");
    s.u_row0 = glGetUniformLocation(s.program, "uRow0");
    s.u_row1 = glGetUniformLocation(s.program, "uRow1");
    s.u_row2 = glGetUniformLocation(s.program, "uRow2");
    s.u_row3 = glGetUniformLocation(s.program, "uRow3");
    s.u_view_z = glGetUniformLocation(s.program, "uViewZ");
    s.u_uv = glGetUniformLocation(s.program, "uUvScaleOffset");
    s.u_fog_params = glGetUniformLocation(s.program, "uFogParams");
    s.u_vertex_color_mul = glGetUniformLocation(s.program, "uVertexColorMul");
    s.u_vertex_color_add = glGetUniformLocation(s.program, "uVertexColorAdd");
    s.u_vertex_color_affine = glGetUniformLocation(s.program, "uVertexColorAffine");

    s.u_tex = glGetUniformLocation(s.program, "uTexture");
    s.u_texture_flip_v = glGetUniformLocation(s.program, "uTextureFlipV");
    s.u_color_test = glGetUniformLocation(s.program, "uColorTest");
    s.u_color_reference = glGetUniformLocation(s.program, "uColorReference");
    s.u_color_mask = glGetUniformLocation(s.program, "uColorMask");
    s.u_texture_enabled = glGetUniformLocation(s.program, "uTextureEnabled");
    s.u_texture_function = glGetUniformLocation(s.program, "uTextureFunction");
    s.u_texture_use_alpha = glGetUniformLocation(s.program, "uTextureUseAlpha");
    s.u_texture_double = glGetUniformLocation(s.program, "uTextureDouble");
    s.u_texture_env = glGetUniformLocation(s.program, "uTextureEnv");
    s.u_alpha_enabled = glGetUniformLocation(s.program, "uAlphaEnabled");
    s.u_alpha_func = glGetUniformLocation(s.program, "uAlphaFunc");
    s.u_alpha_ref = glGetUniformLocation(s.program, "uAlphaRef");
    s.u_alpha_mask = glGetUniformLocation(s.program, "uAlphaMask");
    s.u_fog_enabled = glGetUniformLocation(s.program, "uFogEnabled");
    s.u_fog_color = glGetUniformLocation(s.program, "uFogColor");
    s.u_framebuffer_format = glGetUniformLocation(s.program, "uFramebufferFormat");
    s.u_present_tex = glGetUniformLocation(s.present_program, "uPresentTexture");

    glBindVertexArray(0);
    s.gl_ready = true;
    runtime_log_line("gles: create_gl_objects complete");
    return true;
}

bool ensure_context(GlesState &s, std::string &error) {
    if (!s.enabled) {
        error = "GLES backend disabled";
        return false;
    }

    const std::thread::id current_thread = std::this_thread::get_id();
    if (s.gl_ready && s.gl_thread == current_thread && s.surface != EGL_NO_SURFACE &&
        !s.surface_dirty.load(std::memory_order_acquire)) return true;
    if (s.gl_ready && s.gl_thread != std::thread::id{} &&
        s.gl_thread != current_thread) {
        error = "OpenGL ES context attempted from a second native thread";
        return false;
    }

    if (s.display == EGL_NO_DISPLAY) {
        runtime_log_line("gles: egl initialize begin");
        s.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s.display == EGL_NO_DISPLAY || !eglInitialize(s.display, nullptr, nullptr)) {
            error = egl_error("eglInitialize");
            return false;
        }
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            error = egl_error("eglBindAPI");
            return false;
        }

        const EGLint attributes[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
        };
        EGLint count = 0;
        if (!eglChooseConfig(s.display, attributes, &s.config, 1, &count) || count == 0) {
            error = egl_error("eglChooseConfig");
            return false;
        }

        const EGLint context_attributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        s.context = eglCreateContext(
            s.display, s.config, EGL_NO_CONTEXT, context_attributes);
        if (s.context == EGL_NO_CONTEXT) {
            error = egl_error("eglCreateContext");
            return false;
        }
        runtime_log_line("gles: EGL context created");
    }

    const bool surface_changed =
        s.surface_dirty.exchange(false, std::memory_order_acq_rel);
    if (surface_changed || s.surface == EGL_NO_SURFACE) {
        if (s.surface != EGL_NO_SURFACE) {
            (void)eglMakeCurrent(s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            (void)eglDestroySurface(s.display, s.surface);
            s.surface = EGL_NO_SURFACE;
        }

        ANativeWindow *window = nullptr;
        {
            std::lock_guard<std::mutex> guard(s.window_mutex);
            window = s.window;
            if (window != nullptr) ANativeWindow_acquire(window);
        }
        if (window == nullptr) {
            error = "Android native window is not attached yet";
            return false;
        }

        runtime_log_line("gles: creating window surface");
        s.surface = eglCreateWindowSurface(s.display, s.config, window, nullptr);
        ANativeWindow_release(window);
        if (s.surface == EGL_NO_SURFACE) {
            error = egl_error("eglCreateWindowSurface");
            return false;
        }
        runtime_log_line("gles: window surface created");
    }

    const bool already_current =
        eglGetCurrentContext() == s.context &&
        eglGetCurrentSurface(EGL_DRAW) == s.surface &&
        eglGetCurrentSurface(EGL_READ) == s.surface;
    if (!already_current &&
        !eglMakeCurrent(s.display, s.surface, s.surface, s.context)) {
        error = egl_error("eglMakeCurrent");
        return false;
    }

    if (!s.gl_ready) {
        runtime_log_line("gles: makeCurrent OK, creating GL objects");
        if (!create_gl_objects(s, error)) return false;
        s.gl_thread = current_thread;
        (void)eglSwapInterval(s.display, 0);
        const char *renderer =
            reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        runtime_log_line(std::string("gles ge renderer=") +
                         (renderer != nullptr ? renderer : "unknown"));
    }
    return true;
}

std::uint64_t hash_mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
    return hash;
}

std::uint64_t texture_key(const GeGpuDrawDescriptor &draw) noexcept {
    if (draw.texture_cache_key_hint != 0u) return draw.texture_cache_key_hint;
    std::uint64_t key = 0xCBF29CE484222325ull;
    const std::uint32_t levels =
        draw.texture_level_addresses[0] != 0u && draw.texture_mipmap_enabled
        ? std::min<std::uint32_t>(8u, draw.texture_max_level + 1u)
        : 1u;
    key = hash_mix(key, levels);
    for (std::uint32_t level = 0u; level < levels; ++level) {
        key = hash_mix(key,
            draw.texture_level_addresses[level] != 0u
                ? draw.texture_level_addresses[level] : draw.texture_address);
        key = hash_mix(key,
            draw.texture_level_buffer_widths[level] != 0u
                ? draw.texture_level_buffer_widths[level] : draw.texture_buffer_width);
        key = hash_mix(key,
            draw.texture_level_widths[level] != 0u
                ? draw.texture_level_widths[level] : draw.texture_width);
        key = hash_mix(key,
            draw.texture_level_heights[level] != 0u
                ? draw.texture_level_heights[level] : draw.texture_height);
    }
    key = hash_mix(key, draw.texture_format);
    key = hash_mix(key, draw.clut_address);
    key = hash_mix(key, draw.clut_format);
    key = hash_mix(key, draw.clut_shift);
    key = hash_mix(key, draw.clut_mask);
    key = hash_mix(key, draw.clut_start);
    key = hash_mix(key, draw.clut_checksum);
    key = hash_mix(key, static_cast<std::uint64_t>(draw.texture_swizzled));
    key = hash_mix(key, static_cast<std::uint64_t>(draw.texture_min_linear));
    key = hash_mix(key, static_cast<std::uint64_t>(draw.texture_mag_linear));
    key = hash_mix(key, static_cast<std::uint64_t>(draw.texture_mipmap_enabled));
    key = hash_mix(key, static_cast<std::uint64_t>(draw.texture_mipmap_linear));
    key = hash_mix(key, draw.texture_max_level);
    key = hash_mix(key, draw.texture_level_mode);
    key = hash_mix(key, static_cast<std::uint32_t>(draw.texture_level_offset16));
    key = hash_mix(key, draw.texture_selected_level);
    key = hash_mix(key, static_cast<std::uint64_t>(draw.texture_clamp_u));
    key = hash_mix(key, static_cast<std::uint64_t>(draw.texture_clamp_v));
    return key == 0u ? 1u : key;
}

std::uint64_t texture_lookup_key(const GeGpuDrawDescriptor &draw) noexcept {
    const auto base = texture_key(draw);
    const auto signature = state().texture_versions.resolve(base, draw.texture_content_signature);
    return signature ? hash_mix(base, signature) : base;
}
GeGpuDrawDescriptor snapshot_texture_draw(const GeGpuDrawDescriptor &draw) {
    auto snapshot = draw;
    if (snapshot.texture_enabled)
        snapshot.texture_content_signature = state().texture_versions.resolve(
            texture_key(draw), draw.texture_content_signature);
    return snapshot;
}
auto feedback_target(GlesState &s, std::uint32_t address) {
    if (!android_detail::is_vram_address(address)) return s.targets.end();
    return s.targets.find(android_detail::vram_offset(address));
}
auto feedback_target(const GlesState &s, std::uint32_t address) {
    if (!android_detail::is_vram_address(address)) return s.targets.end();
    return s.targets.find(android_detail::vram_offset(address));
}

std::uint32_t read_texture_entry_limit() noexcept {
    const LcsConfiguration &config = lcs_render_configuration();
    std::uint32_t fallback = config.initialized
        ? std::clamp(config.rendering.texture_cache_entries, 256u, 16384u)
        : 8192u;
    if (const char *text = std::getenv("PSPRECOMP_GE_GPU_TEXTURE_DECODE_LIMIT");
        text != nullptr && *text != '\0') {
        char *end = nullptr;
        const unsigned long value = std::strtoul(text, &end, 10);
        if (end != text && *end == '\0')
            fallback = static_cast<std::uint32_t>(
                std::clamp<unsigned long>(value, 256u, 16384u));
    }
    return fallback;
}

std::uint64_t read_texture_byte_limit() noexcept {
    const LcsConfiguration &config = lcs_render_configuration();
    std::uint64_t mb = config.initialized
        ? std::clamp<std::uint32_t>(config.rendering.texture_cache_mb, 32u, 512u)
        : 256u;
    if (const char *text = std::getenv("PSPRECOMP_GE_GPU_TEXTURE_CACHE_MB");
        text != nullptr && *text != '\0') {
        char *end = nullptr;
        const unsigned long value = std::strtoul(text, &end, 10);
        if (end != text && *end == '\0')
            mb = std::clamp<unsigned long>(value, 32u, 512u);
    }
    return mb * 1024ull * 1024ull;
}

void trim_texture_cache(GlesState &s, bool aggressive) {
    const auto over_budget = [&]() {
        return s.textures.size() > s.texture_cache_entry_limit ||
               s.texture_cache_bytes > s.texture_cache_byte_limit;
    };

    while (over_budget()) {
        auto victim = s.textures.end();
        for (auto it = s.textures.begin(); it != s.textures.end(); ++it) {
            // Never delete a texture referenced by batches accumulated for the
            // frame currently being drawn.
            if (it->second.last_used_epoch >= s.frame_epoch) continue;
            if (!aggressive && it->second.last_used_epoch + 2u >= s.frame_epoch)
                continue;
            if (victim == s.textures.end() ||
                it->second.last_used_epoch < victim->second.last_used_epoch)
                victim = it;
        }
        if (victim == s.textures.end()) break;

        if (victim->second.id != 0u) glDeleteTextures(1, &victim->second.id);
        s.texture_cache_bytes -=
            std::min(s.texture_cache_bytes, victim->second.byte_size);
        s.textures.erase(victim);
        ++s.report.evicted_textures;
    }
}

GlesTarget &target_metadata(GlesState &s, std::uint32_t address) {
    address &= 0x001FFFF0u;
    auto [it, inserted] = s.targets.try_emplace(address);
    if (inserted) {
        it->second.address = address;
        it->second.logical_width = s.display_logical_width;
        it->second.logical_height = s.display_logical_height;
    }
    return it->second;
}

bool ensure_target(GlesState &s, GlesTarget &target, std::string &error) {
    const std::uint32_t logical_width = std::max<std::uint32_t>(1u, target.logical_width);
    const std::uint32_t logical_height = std::max<std::uint32_t>(1u, target.logical_height);
    const std::uint32_t render_width = logical_width * s.scale;
    const std::uint32_t render_height = logical_height * s.scale;

    if (target.fbo != 0u &&
        target.render_width == render_width &&
        target.render_height == render_height)
        return true;

    delete_target(target);
    target.render_width = render_width;
    target.render_height = render_height;

    if (s.frame_epoch <= 12u) {
        runtime_log_line("gles: create target " +
                         std::to_string(render_width) + "x" +
                         std::to_string(render_height) +
                         " address=" + std::to_string(target.address));
    }
    glGenTextures(1, &target.color);
    glBindTexture(GL_TEXTURE_2D, target.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 static_cast<GLsizei>(render_width),
                 static_cast<GLsizei>(render_height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenRenderbuffers(1, &target.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          static_cast<GLsizei>(render_width),
                          static_cast<GLsizei>(render_height));

    glGenFramebuffers(1, &target.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, target.color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, target.depth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        error = "OpenGL ES framebuffer incomplete";
        delete_target(target);
        return false;
    }

    target.cleared = false;
    ++s.report.framebuffer_targets_observed;
    return true;
}

GLenum depth_func(std::uint32_t function) noexcept {
    switch (function & 7u) {
    case 0u: return GL_NEVER;
    case 1u: return GL_ALWAYS;
    case 2u: return GL_EQUAL;
    case 3u: return GL_NOTEQUAL;
    case 4u: return GL_LESS;
    case 5u: return GL_LEQUAL;
    case 6u: return GL_GREATER;
    case 7u: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

std::size_t blend_variant(const GeGpuDrawDescriptor &draw) noexcept {
    if (!draw.blend_enabled || draw.clear_mode) return 0u;
    const std::uint32_t eq = draw.blend_equation & 7u;
    const std::uint32_t src = draw.blend_source_factor & 0xFu;
    const std::uint32_t dst = draw.blend_dest_factor & 0xFu;
    if (eq == 0u && src == 2u && dst == 3u) return 1u;
    if (eq == 0u && src == 10u && dst == 10u) {
        const std::uint32_t fs = draw.blend_fix_source & 0x00FFFFFFu;
        const std::uint32_t fd = draw.blend_fix_dest & 0x00FFFFFFu;
        if (fs == 0x00FFFFFFu && fd == 0u) return 2u;
        if (fs == 0x00FFFFFFu && fd == 0x00FFFFFFu) return 3u;
        bool complements = true;
        for (std::uint32_t shift = 0u; shift < 24u; shift += 8u)
            complements &=
                (((fs >> shift) & 0xFFu) + ((fd >> shift) & 0xFFu)) == 0xFFu;
        if (complements) return 4u;
    }
    if (eq == 0u && src == 2u && dst == 10u &&
        (draw.blend_fix_dest & 0x00FFFFFFu) == 0x00FFFFFFu) return 5u;
    return 0u;
}

void apply_draw_state(const GeGpuDrawDescriptor &draw) {
    if (draw.depth_test_enabled || (draw.clear_mode && draw.clear_depth)) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(draw.clear_mode ? GL_ALWAYS : depth_func(draw.depth_function));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(draw.depth_write_enabled ? GL_TRUE : GL_FALSE);

    const std::size_t variant = blend_variant(draw);
    if (variant == 0u || variant == 2u) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        switch (variant) {
        case 1u:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case 3u:
            glBlendFunc(GL_ONE, GL_ONE);
            break;
        case 4u: {
            const std::uint32_t fix = draw.blend_fix_source & 0x00FFFFFFu;
            glBlendColor(
                static_cast<float>(fix & 0xFFu) / 255.0f,
                static_cast<float>((fix >> 8u) & 0xFFu) / 255.0f,
                static_cast<float>((fix >> 16u) & 0xFFu) / 255.0f,
                1.0f);
            glBlendFunc(GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR);
            break;
        }
        case 5u:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    }

    const auto writable = [&](std::uint32_t channel) {
        return ((draw.color_write_mask >> (channel * 8u)) & 0xFFu) != 0xFFu;
    };
    glColorMask(writable(0u), writable(1u), writable(2u), writable(3u));
}

std::array<float, 4> transform_row(
    const GeGpuHardwareTransform &hw, std::size_t row) noexcept {
    return {hw.model_to_clip[row], hw.model_to_clip[4u + row],
            hw.model_to_clip[8u + row], hw.model_to_clip[12u + row]};
}

std::array<float, 4> add_scaled(const std::array<float, 4> &a, float sa,
                                const std::array<float, 4> &b, float sb) noexcept {
    return {a[0] * sa + b[0] * sb,
            a[1] * sa + b[1] * sb,
            a[2] * sa + b[2] * sb,
            a[3] * sa + b[3] * sb};
}

void set_transform_uniforms(
    GlesState &s, const GlesBatch &batch,
    std::uint32_t logical_width, std::uint32_t logical_height) {
    glUniform2f(s.u_logical_size,
                static_cast<float>(logical_width),
                static_cast<float>(logical_height));

    if (!batch.hardware_transform) {
        glUniform1i(s.u_mode, 2);
        glUniform1i(s.u_vertex_color_affine, 0);
        return;
    }

    const GeGpuHardwareTransform &hw = batch.transform;
    const auto clip_x = transform_row(hw, 0u);
    const auto clip_y = transform_row(hw, 1u);
    const auto clip_z = transform_row(hw, 2u);
    const auto clip_w = transform_row(hw, 3u);
    float x_a = hw.viewport_scale_x *
        (2.0f / static_cast<float>(std::max<std::uint32_t>(1u, logical_width)));
    float x_b = (hw.viewport_center_x - hw.viewport_offset_x) *
        (2.0f / static_cast<float>(std::max<std::uint32_t>(1u, logical_width))) - 1.0f;
    // Match the CPU ultrawide correction when hardware transform is enabled:
    // compress clip X before the final SurfaceView stretch, yielding extra
    // horizontal field of view instead of stretched geometry.
    const float ultrawide = android_host::ultrawide_x_scale();
    if (std::isfinite(ultrawide) && ultrawide > 0.0f) {
        x_a *= ultrawide;
        x_b *= ultrawide;
    }
    const float y_a = hw.viewport_scale_y *
        (2.0f / static_cast<float>(std::max<std::uint32_t>(1u, logical_height)));
    const float y_b = (hw.viewport_center_y - hw.viewport_offset_y) *
        (2.0f / static_cast<float>(std::max<std::uint32_t>(1u, logical_height))) - 1.0f;
    constexpr float inv_depth = 1.0f / 65535.0f;
    const float z_a = hw.viewport_scale_z * inv_depth;
    const float z_b = hw.viewport_center_z * inv_depth;

    const auto row0 = add_scaled(clip_x, x_a, clip_w, x_b);
    const auto row1 = add_scaled(clip_y, -y_a, clip_w, -y_b);
    const auto row2 = add_scaled(clip_z, z_a, clip_w, z_b);

    glUniform1i(s.u_mode, 1);
    glUniform4fv(s.u_row0, 1, row0.data());
    glUniform4fv(s.u_row1, 1, row1.data());
    glUniform4fv(s.u_row2, 1, row2.data());
    glUniform4fv(s.u_row3, 1, clip_w.data());
    glUniform4fv(s.u_view_z, 1, hw.model_to_view_z.data());
    glUniform4f(s.u_uv, hw.uv_scale_u, hw.uv_scale_v,
                hw.uv_offset_u, hw.uv_offset_v);
    glUniform2f(s.u_fog_params, hw.fog_end, hw.fog_slope);
    glUniform4fv(s.u_vertex_color_mul, 1, hw.vertex_color_mul.data());
    glUniform4fv(s.u_vertex_color_add, 1, hw.vertex_color_add.data());
    glUniform1i(s.u_vertex_color_affine, hw.vertex_color_affine ? 1 : 0);
}

void set_pixel_uniforms(GlesState &s, const GeGpuDrawDescriptor &draw,
                        bool textured) {
    glUniform1i(s.u_tex, 0);
    glUniform1i(s.u_texture_flip_v,
        draw.texture_enabled && feedback_target(s, draw.texture_address) != s.targets.end());
    glUniform1i(s.u_color_test, draw.color_test_enabled && !draw.clear_mode
        ? static_cast<GLint>(draw.color_test_function & 3u) : -1);
    const auto rgb_uniform = [](GLint location, std::uint32_t packed) {
        glUniform3i(location, packed & 255u, (packed >> 8u) & 255u, (packed >> 16u) & 255u);
    };
    rgb_uniform(s.u_color_reference, draw.color_test_reference);
    rgb_uniform(s.u_color_mask, draw.color_test_mask);
    glUniform1i(s.u_texture_enabled, textured ? 1 : 0);
    glUniform1i(s.u_texture_function, static_cast<GLint>(draw.texture_function & 7u));
    glUniform1i(s.u_texture_use_alpha, draw.texture_use_alpha ? 1 : 0);
    glUniform1i(s.u_texture_double, draw.texture_double_color ? 1 : 0);
    glUniform3f(s.u_texture_env,
                static_cast<float>(draw.texture_env & 0xFFu) / 255.0f,
                static_cast<float>((draw.texture_env >> 8u) & 0xFFu) / 255.0f,
                static_cast<float>((draw.texture_env >> 16u) & 0xFFu) / 255.0f);

    glUniform1i(s.u_alpha_enabled, draw.alpha_test_enabled ? 1 : 0);
    glUniform1i(s.u_alpha_func, static_cast<GLint>(draw.alpha_function & 7u));
    glUniform1i(s.u_alpha_ref, static_cast<GLint>(draw.alpha_reference & 0xFFu));
    glUniform1i(s.u_alpha_mask, static_cast<GLint>(draw.alpha_mask & 0xFFu));

    glUniform1i(s.u_fog_enabled, draw.fog_enabled ? 1 : 0);
    glUniform3f(s.u_fog_color,
                static_cast<float>(draw.fog_color & 0xFFu) / 255.0f,
                static_cast<float>((draw.fog_color >> 8u) & 0xFFu) / 255.0f,
                static_cast<float>((draw.fog_color >> 16u) & 0xFFu) / 255.0f);
    glUniform1i(s.u_framebuffer_format,
                static_cast<GLint>(draw.framebuffer_format & 3u));
}

GLuint texture_for_draw(GlesState &s, const GeGpuDrawDescriptor &draw,
                        GlesTarget *current_target) {
    if (!draw.texture_enabled) return 0u;

    const std::uint32_t address = draw.texture_address & 0x001FFFF0u;
    const auto framebuffer = feedback_target(s, draw.texture_address);
    if (framebuffer != s.targets.end() && framebuffer->second.color != 0u) {
        GlesTarget &source = framebuffer->second;
        if (current_target == &source) {
            if (source.feedback == 0u) {
                glGenTextures(1, &source.feedback);
                glBindTexture(GL_TEXTURE_2D, source.feedback);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             static_cast<GLsizei>(source.render_width),
                             static_cast<GLsizei>(source.render_height),
                             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            glBindTexture(GL_TEXTURE_2D, source.feedback);
            glCopyTexSubImage2D(
                GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                static_cast<GLsizei>(source.render_width),
                static_cast<GLsizei>(source.render_height));
            ++s.report.vram_feedback_refreshes;
            return source.feedback;
        }
        return source.color;
    }

    const auto found = s.textures.find(texture_lookup_key(draw));
    if (found == s.textures.end() || found->second.id == 0u) return 0u;
    found->second.last_used_epoch = s.frame_epoch;
    return found->second.id;
}

void configure_texture_sampling(const GeGpuDrawDescriptor &draw, GLuint texture) {
    if (texture == 0u) return;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    draw.texture_clamp_u ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    draw.texture_clamp_v ? GL_CLAMP_TO_EDGE : GL_REPEAT);

    GLint min_filter = draw.texture_min_linear ? GL_LINEAR : GL_NEAREST;
    // PSP may request mip filtering with a one-level texture. GLES is strict
    // about texture completeness, so only select a mip filter when at least one
    // additional mip level is declared.
    if (draw.texture_mipmap_enabled && draw.texture_max_level > 0u) {
        if (draw.texture_mipmap_linear) {
            min_filter = draw.texture_min_linear
                ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
        } else {
            min_filter = draw.texture_min_linear
                ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    draw.texture_mag_linear ? GL_LINEAR : GL_NEAREST);
}

bool draw_batch(GlesState &s, GlesBatch &batch, std::string &error) {
    GlesTarget &target = target_metadata(s, batch.draw.framebuffer_address);
    if (s.frame_epoch <= 12u) {
        runtime_log_line("gles: draw frame=" + std::to_string(s.frame_epoch) +
                         " fb=" + std::to_string(target.address) +
                         " verts=" + std::to_string(batch.vertices.size()) +
                         " idx=" + std::to_string(batch.indices.size()));
    }
    if (!ensure_target(s, target, error)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0,
               static_cast<GLsizei>(target.render_width),
               static_cast<GLsizei>(target.render_height));

    if (!target.cleared) {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClearDepthf(0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        target.cleared = true;
    }

    const std::uint32_t logical_width =
        std::max<std::uint32_t>(1u, target.logical_width);
    const std::uint32_t logical_height =
        std::max<std::uint32_t>(1u, target.logical_height);

    const auto scale_x = [&](std::int32_t value) {
        return static_cast<GLint>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(value) * target.render_width /
                logical_width,
            0, static_cast<std::int64_t>(target.render_width)));
    };
    const auto scale_y = [&](std::int32_t value) {
        return static_cast<GLint>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(value) * target.render_height /
                logical_height,
            0, static_cast<std::int64_t>(target.render_height)));
    };

    const GLint left = scale_x(batch.draw.scissor_x0);
    const GLint right = scale_x(batch.draw.scissor_x1 + 1);
    const GLint top = scale_y(batch.draw.scissor_y0);
    const GLint bottom = scale_y(batch.draw.scissor_y1 + 1);
    if (right <= left || bottom <= top) return true;

    glEnable(GL_SCISSOR_TEST);
    glScissor(left,
              static_cast<GLint>(target.render_height) - bottom,
              right - left, bottom - top);

    apply_draw_state(batch.draw);
    glDisable(GL_CULL_FACE);

    glUseProgram(s.program);
    set_transform_uniforms(s, batch, logical_width, logical_height);

    glActiveTexture(GL_TEXTURE0);
    const GLuint texture = texture_for_draw(s, batch.draw, &target);
    const bool textured = batch.draw.texture_enabled && texture != 0u;
    if (textured) configure_texture_sampling(batch.draw, texture);
    else glBindTexture(GL_TEXTURE_2D, 0u);
    set_pixel_uniforms(s, batch.draw, textured);

    glBindVertexArray(s.vao);

    GLenum mode = GL_TRIANGLES;
    if (batch.hardware_transform && batch.transform.primitive == 4u &&
        batch.indices.empty())
        mode = GL_TRIANGLE_STRIP;

    if (!batch.indices.empty()) {
        const std::uintptr_t index_offset =
            static_cast<std::uintptr_t>(batch.first_index) * sizeof(std::uint32_t);
        glDrawElements(mode, static_cast<GLsizei>(batch.indices.size()),
                       GL_UNSIGNED_INT,
                       reinterpret_cast<const void *>(index_offset));
    } else {
        glDrawArrays(mode, static_cast<GLint>(batch.first_vertex),
                     static_cast<GLsizei>(batch.vertices.size()));
    }

    ++s.report.game_draw_calls;
    s.report.game_vertices += batch.indices.empty()
        ? batch.vertices.size() : batch.indices.size();
    s.report.game_triangles += batch.indices.empty()
        ? (mode == GL_TRIANGLE_STRIP
            ? (batch.vertices.size() > 2u ? batch.vertices.size() - 2u : 0u)
            : batch.vertices.size() / 3u)
        : (mode == GL_TRIANGLE_STRIP
            ? (batch.indices.size() > 2u ? batch.indices.size() - 2u : 0u)
            : batch.indices.size() / 3u);
    if (batch.draw.color_test_enabled) ++s.perf_color_tests;
    if (textured) ++s.report.textured_game_draw_calls;
    if (batch.draw.depth_test_enabled) ++s.report.depth_tested_game_draw_calls;
    if (batch.draw.depth_write_enabled) ++s.report.depth_writing_game_draw_calls;
    if (batch.draw.alpha_test_enabled) ++s.report.alpha_tested_game_draw_calls;
    if (batch.draw.fog_enabled) ++s.report.fogged_game_draw_calls;
    return true;
}

bool present_target(GlesState &s, GlesTarget &target, std::string &error) {
    if (!ensure_target(s, target, error)) return false;

    EGLint surface_width = 0;
    EGLint surface_height = 0;
    if (!eglQuerySurface(s.display, s.surface, EGL_WIDTH, &surface_width) ||
        !eglQuerySurface(s.display, s.surface, EGL_HEIGHT, &surface_height) ||
        surface_width <= 0 || surface_height <= 0) {
        error = egl_error("eglQuerySurface");
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, surface_width, surface_height);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(s.present_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(s.u_present_tex, 0);
    glBindVertexArray(s.present_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (eglSwapBuffers(s.display, s.surface) != EGL_TRUE) {
        const EGLint swap_error = eglGetError();
        if (swap_error == EGL_BAD_SURFACE || swap_error == EGL_BAD_NATIVE_WINDOW) {
            runtime_log_line("gles: Android surface lost; waiting for replacement");
            (void)eglMakeCurrent(
                s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (s.surface != EGL_NO_SURFACE)
                (void)eglDestroySurface(s.display, s.surface);
            s.surface = EGL_NO_SURFACE;
            s.surface_dirty.store(true, std::memory_order_release);
            s.direct_present_ok = false;
            return false;
        }
        char buffer[96]{};
        std::snprintf(buffer, sizeof(buffer), "eglSwapBuffers (EGL=0x%04X)",
                      static_cast<unsigned>(swap_error));
        error = buffer;
        return false;
    }

    s.direct_present_ok = true;
    s.presented_framebuffer = target.address;
    s.report.gpu_frame_presented_to_window = true;
    s.report.presented_framebuffer_target = target.address;
    return true;
}

void destroy_backend(GlesState &s) noexcept {
    if (s.display != EGL_NO_DISPLAY && s.context != EGL_NO_CONTEXT &&
        s.surface != EGL_NO_SURFACE) {
        if (eglMakeCurrent(s.display, s.surface, s.surface, s.context))
            destroy_gl_objects(s);
        (void)eglMakeCurrent(
            s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    if (s.display != EGL_NO_DISPLAY && s.surface != EGL_NO_SURFACE)
        (void)eglDestroySurface(s.display, s.surface);
    if (s.display != EGL_NO_DISPLAY && s.context != EGL_NO_CONTEXT)
        (void)eglDestroyContext(s.display, s.context);
    if (s.display != EGL_NO_DISPLAY)
        (void)eglTerminate(s.display);

    s.surface = EGL_NO_SURFACE;
    s.context = EGL_NO_CONTEXT;
    s.display = EGL_NO_DISPLAY;
    s.config = {};
    s.batches.clear();
    s.last_texture_rgba.clear();

    {
        std::lock_guard<std::mutex> guard(s.window_mutex);
        if (s.window != nullptr) ANativeWindow_release(s.window);
        s.window = nullptr;
    }

    s.gl_thread = {};
    s.initialized = false;
    s.enabled = false;
    s.direct_present_ok = false;
    s.presented_framebuffer = 0u;
}

}  // namespace

bool initialize_ge_gpu_backend(std::string &error) {
    GlesState &s = state();
    destroy_backend(s);
    s.report = {};
    s.scale = read_scale();

    const char *backend = std::getenv("PSPRECOMP_GE_BACKEND");
    if (backend != nullptr &&
        (std::strcmp(backend, "software") == 0 ||
         std::strcmp(backend, "Software") == 0)) {
        s.report.requested = GeGpuBackendKind::Software;
        s.report.active = GeGpuBackendKind::Software;
        s.report.message = "Software GE backend requested";
        error.clear();
        return true;
    }

    s.texture_cache_entry_limit = read_texture_entry_limit();
    s.texture_cache_byte_limit = read_texture_byte_limit();
    s.texture_cache_bytes = 0u;

    s.report.requested = GeGpuBackendKind::OpenGLES;
    s.report.active = GeGpuBackendKind::OpenGLES;
    s.report.message = "OpenGL ES 3 mobile GE backend";
    s.report.frames_in_flight_capacity = 2u;
    s.report.loader_opened = true;
    s.report.instance_created = true;
    s.report.device_created = true;
    s.report.transfer_buffer_created = true;
    s.report.command_pool_created = true;
    s.report.offscreen_image_created = true;
    s.report.offscreen_image_memory_bound = true;
    s.report.offscreen_image_view_created = true;
    s.report.render_pass_created = true;
    s.report.framebuffer_created = true;
    s.report.shader_modules_created = true;
    s.report.graphics_pipeline_created = true;
    s.report.texture_descriptor_layout_created = true;
    s.report.texture_descriptor_pool_created = true;
    s.report.textured_shader_modules_created = true;
    s.report.textured_pipeline_created = true;
    s.report.depth_image_created = true;
    s.report.depth_image_memory_bound = true;
    s.report.depth_image_view_created = true;
    s.report.depth_attachment_active = true;
    s.report.alpha_test_shader_active = true;
    s.report.standard_alpha_blend_pipeline_active = true;
    s.report.observed_blend_modes_pipeline_active = true;
    s.report.color_write_mask_pipeline_active = true;
    s.report.base_texture_formats_active = true;
    s.report.fog_shader_active = true;
    s.report.full_mip_chain_active = true;
    s.report.mipmap_state_active = true;

    s.initialized = true;
    s.enabled = true;
    s.display_logical_width = kReferenceWidth;
    s.display_logical_height = kReferenceHeight;
    error.clear();
    return true;
}

void shutdown_ge_gpu_backend() noexcept {
    GlesState &s = state();
    if (s.enabled) runtime_log_line("gles ge shutdown");
    destroy_backend(s);
    s.report = {};
}

bool ge_gpu_backend_active() noexcept {
    return state().enabled;
}

bool ge_gpu_backend_transfer_ready() noexcept {
    GlesState &s = state();
    if (!s.enabled) return false;
    std::string error;
    return ensure_context(s, error);
}

bool ge_gpu_backend_graphics_ready() noexcept {
    GlesState &s = state();
    if (!s.enabled) return false;
    std::string error;
    if (ensure_context(s, error)) return true;
    if (!error.empty()) s.report.message = error;
    return false;
}

void ge_gpu_backend_record_draw(const GeGpuDrawDescriptor &draw) noexcept {
    GlesState &s = state();
    if (!s.enabled) return;

    if (draw.texture_enabled && draw.texture_content_signature != 0u)
        s.texture_versions.observe(texture_key(draw), draw.texture_content_signature, s.frame_epoch);

    ++s.report.draw_calls;
    s.report.vertices += draw.vertex_count;
    if (draw.texture_enabled) ++s.report.textured_draw_calls;

    GlesTarget &target = target_metadata(s, draw.framebuffer_address);
    target.logical_width = std::max<std::uint32_t>(
        target.logical_width,
        std::max<std::uint32_t>(
            static_cast<std::uint32_t>(std::max(0, draw.scissor_x1 + 1)),
            std::min<std::uint32_t>(
                std::max<std::uint32_t>(1u, draw.framebuffer_stride), 1024u)));
    target.logical_height = std::max<std::uint32_t>(
        target.logical_height,
        static_cast<std::uint32_t>(std::max(1, draw.scissor_y1 + 1)));
}

void ge_gpu_backend_observe_camera(
    const std::array<float, 12> &, const std::array<float, 16> &,
    const std::array<float, 6> &, const std::array<float, 3> &,
    const GeGpuDrawDescriptor &, std::uint32_t) noexcept {}

bool ge_gpu_backend_stage_vertices(
    const GeGpuDrawDescriptor &, std::span<const GeGpuVertex>) noexcept {
    return true;
}

bool ge_gpu_backend_texture_needed(
    const GeGpuDrawDescriptor &draw) noexcept {
    GlesState &s = state();
    if (!s.enabled || !draw.texture_enabled || draw.texture_format > 10u ||
        draw.texture_width == 0u || draw.texture_height == 0u)
        return false;

    const std::uint32_t feedback_address =
        draw.texture_address & 0x001FFFF0u;
    if (feedback_target(s, draw.texture_address) != s.targets.end()) {
        ++s.report.texture_cache_hits;
        return false;
    }

    ++s.report.texture_decode_requests;
    const auto found = s.textures.find(texture_lookup_key(draw));
    if (found == s.textures.end()) return true;

    found->second.signature_epoch = s.frame_epoch;
    found->second.last_used_epoch = s.frame_epoch;
    if (draw.texture_content_signature != 0u &&
        found->second.signature != draw.texture_content_signature)
        return true;

    ++s.report.texture_cache_hits;
    return false;
}

void ge_gpu_backend_prepare_texture_keys(
    GeGpuDrawDescriptor &draw) noexcept {
    if (!draw.texture_enabled) {
        draw.texture_cache_key_hint = 0u;
        draw.texture_image_key_hint = 0u;
        return;
    }
    draw.texture_cache_key_hint = 0u;
    draw.texture_image_key_hint = 0u;
    const std::uint64_t key = texture_key(draw);
    draw.texture_cache_key_hint = key;
    draw.texture_image_key_hint = key;
}

bool ge_gpu_backend_texture_signature_needed(const GeGpuDrawDescriptor &draw) noexcept {
    auto &s = state();
    if (!s.enabled || !draw.texture_enabled || !draw.texture_width || !draw.texture_height)
        return false;
    if (feedback_target(s, draw.texture_address) != s.targets.end()) return false;
    return s.texture_versions.needs_check(texture_key(draw), s.frame_epoch);
}

bool ge_gpu_backend_is_framebuffer_feedback_texture(
    const GeGpuDrawDescriptor &draw) noexcept {
    const GlesState &s = state();
    return feedback_target(s, draw.texture_address) != s.targets.end();
}

GeGpuWidescreenHud ge_gpu_backend_widescreen_hud(
    const GeGpuDrawDescriptor &) noexcept {
    // Android screen/HUD correction is already performed in ge_renderer.cpp.
    return {};
}

void ge_gpu_backend_note_through_extent(
    const GeGpuDrawDescriptor &, float, float) noexcept {}

bool ge_gpu_backend_adopt_shared_texture(
    const GeGpuDrawDescriptor &draw) noexcept {
    return ge_gpu_backend_is_framebuffer_feedback_texture(draw);
}

bool ge_gpu_backend_texture_available(
    const GeGpuDrawDescriptor &draw) noexcept {
    GlesState &s = state();
    if (!draw.texture_enabled) return false;

    const auto target =
        feedback_target(s, draw.texture_address);
    if (target != s.targets.end()) return true;

    const auto found = s.textures.find(texture_lookup_key(draw));
    if (found == s.textures.end() || found->second.id == 0u) return false;
    found->second.last_used_epoch = s.frame_epoch;
    return true;
}

bool ge_gpu_backend_upload_decoded_texture(
    const GeGpuDrawDescriptor &draw,
    std::uint32_t width, std::uint32_t height,
    std::span<const std::byte> rgba8) noexcept {
    if (width == 0u || height == 0u || rgba8.empty()) return false;
    std::vector<std::byte> packed(rgba8.begin(), rgba8.end());
    return ge_gpu_backend_upload_decoded_texture_chain_packed(
        draw, width, height, 1u, std::move(packed));
}

bool ge_gpu_backend_upload_decoded_texture_chain(
    const GeGpuDrawDescriptor &draw,
    std::span<const GeGpuDecodedMipLevel> levels) noexcept {
    if (levels.empty()) return false;
    std::size_t total = 0u;
    for (const auto &level : levels)
        total += level.rgba8.size();
    std::vector<std::byte> packed;
    try { packed.reserve(total); } catch (...) { return false; }
    for (const auto &level : levels)
        packed.insert(packed.end(), level.rgba8.begin(), level.rgba8.end());
    return ge_gpu_backend_upload_decoded_texture_chain_packed(
        draw, levels.front().width, levels.front().height,
        static_cast<std::uint32_t>(levels.size()), std::move(packed));
}

bool ge_gpu_backend_upload_decoded_texture_chain_packed(
    const GeGpuDrawDescriptor &draw,
    std::uint32_t base_width, std::uint32_t base_height,
    std::uint32_t mip_levels, std::vector<std::byte> rgba8) noexcept {
    GlesState &s = state();
    if (!s.enabled || base_width == 0u || base_height == 0u ||
        mip_levels == 0u || rgba8.empty())
        return false;
    if (base_width > 2048u || base_height > 2048u || mip_levels > 8u) {
        ++s.report.rejected_texture_decodes;
        runtime_log_line("gles: rejected oversized texture " +
                         std::to_string(base_width) + "x" +
                         std::to_string(base_height));
        return false;
    }

    std::string error;
    if (!ensure_context(s, error)) {
        s.report.message = error;
        return false;
    }

    const std::uint64_t key = texture_lookup_key(draw);

    auto [texture_it, inserted] = s.textures.try_emplace(key);
    GlesTexture &texture = texture_it->second;
    if (texture.id == 0u) {
        glGenTextures(1, &texture.id);
        ++s.report.texture_images_created;
    }

    glBindTexture(GL_TEXTURE_2D, texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const bool same_layout =
        !inserted &&
        texture.width == base_width &&
        texture.height == base_height &&
        texture.levels == mip_levels;

    if (!same_layout) {
        s.texture_cache_bytes -=
            std::min(s.texture_cache_bytes, texture.byte_size);
        texture.byte_size = rgba8.size();
        s.texture_cache_bytes += texture.byte_size;
    }

    std::size_t offset = 0u;
    std::uint32_t width = base_width;
    std::uint32_t height = base_height;
    for (std::uint32_t level = 0u; level < mip_levels; ++level) {
        const std::size_t bytes =
            static_cast<std::size_t>(width) * height * 4u;
        if (offset + bytes > rgba8.size()) return false;

        if (same_layout) {
            glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                            0, 0,
                            static_cast<GLsizei>(width),
                            static_cast<GLsizei>(height),
                            GL_RGBA, GL_UNSIGNED_BYTE, rgba8.data() + offset);
        } else {
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA8,
                         static_cast<GLsizei>(width),
                         static_cast<GLsizei>(height),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba8.data() + offset);
        }

        offset += bytes;
        width = std::max<std::uint32_t>(1u, width >> 1u);
        height = std::max<std::uint32_t>(1u, height >> 1u);
        ++s.report.uploaded_mip_levels;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                    static_cast<GLint>(mip_levels - 1u));
    configure_texture_sampling(draw, texture.id);
    texture.width = base_width;
    texture.height = base_height;
    texture.levels = mip_levels;
    texture.signature = draw.texture_content_signature;
    texture.signature_epoch = s.frame_epoch;
    texture.last_used_epoch = s.frame_epoch;

    // Only trim entries not referenced by this frame. Temporary overflow is
    // safer than deleting a texture still needed by queued draw batches.
    trim_texture_cache(s, false);

    s.last_texture_rgba = rgba8;
    ++s.report.decoded_texture_uploads;
    s.report.decoded_texture_bytes += rgba8.size();
    s.report.texture_image_upload_bytes += rgba8.size();
    s.report.last_texture_key = key;
    s.report.last_texture_width = base_width;
    s.report.last_texture_height = base_height;
    s.report.last_texture_format = draw.texture_format;
    return glGetError() == GL_NO_ERROR;
}

bool ge_gpu_backend_copy_last_texture_rgba(
    std::span<std::byte> destination) noexcept {
    const GlesState &s = state();
    if (s.last_texture_rgba.empty() ||
        destination.size() < s.last_texture_rgba.size())
        return false;
    std::memcpy(destination.data(),
                s.last_texture_rgba.data(), s.last_texture_rgba.size());
    return true;
}

bool gles_draw_state_compatible(
    const GeGpuDrawDescriptor &a,
    const GeGpuDrawDescriptor &b) noexcept {
    if ((a.framebuffer_address & 0x001FFFF0u) !=
        (b.framebuffer_address & 0x001FFFF0u)) return false;
    if (a.framebuffer_format != b.framebuffer_format ||
        a.framebuffer_stride != b.framebuffer_stride) return false;

    if (a.scissor_x0 != b.scissor_x0 || a.scissor_y0 != b.scissor_y0 ||
        a.scissor_x1 != b.scissor_x1 || a.scissor_y1 != b.scissor_y1)
        return false;

    // Feedback reads are order-dependent and must not be merged.
    if ((a.texture_enabled && feedback_target(state(), a.texture_address) != state().targets.end()) ||
        (b.texture_enabled && feedback_target(state(), b.texture_address) != state().targets.end())) return false;
    if (a.color_test_enabled != b.color_test_enabled ||
        a.color_test_function != b.color_test_function ||
        a.color_test_reference != b.color_test_reference ||
        a.color_test_mask != b.color_test_mask) return false;
    if (a.texture_enabled != b.texture_enabled) return false;
    if (a.texture_enabled) {
        if (texture_lookup_key(a) != texture_lookup_key(b)) return false;
        if (a.texture_function != b.texture_function ||
            a.texture_use_alpha != b.texture_use_alpha ||
            a.texture_double_color != b.texture_double_color ||
            a.texture_env != b.texture_env ||
            a.texture_min_linear != b.texture_min_linear ||
            a.texture_mag_linear != b.texture_mag_linear ||
            a.texture_mipmap_enabled != b.texture_mipmap_enabled ||
            a.texture_mipmap_linear != b.texture_mipmap_linear ||
            a.texture_clamp_u != b.texture_clamp_u ||
            a.texture_clamp_v != b.texture_clamp_v)
            return false;
    }

    return a.blend_enabled == b.blend_enabled &&
           a.blend_equation == b.blend_equation &&
           a.blend_source_factor == b.blend_source_factor &&
           a.blend_dest_factor == b.blend_dest_factor &&
           a.blend_fix_source == b.blend_fix_source &&
           a.blend_fix_dest == b.blend_fix_dest &&
           a.color_write_mask == b.color_write_mask &&
           a.alpha_test_enabled == b.alpha_test_enabled &&
           a.alpha_function == b.alpha_function &&
           a.alpha_reference == b.alpha_reference &&
           a.alpha_mask == b.alpha_mask &&
           a.depth_test_enabled == b.depth_test_enabled &&
           a.depth_write_enabled == b.depth_write_enabled &&
           a.depth_function == b.depth_function &&
           a.fog_enabled == b.fog_enabled &&
           a.fog_color == b.fog_color &&
           a.clear_mode == b.clear_mode &&
           a.clear_color == b.clear_color &&
           a.clear_alpha == b.clear_alpha &&
           a.clear_depth == b.clear_depth;
}

bool append_or_merge_color_batch(
    GlesState &s,
    const GeGpuDrawDescriptor &input_draw,
    std::span<const GeGpuVertex> vertices) {
    const GeGpuDrawDescriptor draw = snapshot_texture_draw(input_draw);
    const bool sampled_texture =
        draw.texture_enabled && ge_gpu_backend_texture_available(draw);
    const bool normalize_uv =
        sampled_texture && draw.texture_width != 0u && draw.texture_height != 0u;
    const float inv_width = normalize_uv
        ? 1.0f / static_cast<float>(draw.texture_width) : 1.0f;
    const float inv_height = normalize_uv
        ? 1.0f / static_cast<float>(draw.texture_height) : 1.0f;

    const auto append_vertices = [&](std::vector<GeGpuVertex> &destination) {
        const std::size_t first = destination.size();
        destination.insert(destination.end(), vertices.begin(), vertices.end());
        if (normalize_uv) {
            for (std::size_t i = first; i < destination.size(); ++i) {
                destination[i].u *= inv_width;
                destination[i].v *= inv_height;
            }
        }
    };

    if (!s.batches.empty()) {
        GlesBatch &last = s.batches.back();
        if (!last.hardware_transform && last.indices.empty() &&
            gles_draw_state_compatible(last.draw, draw)) {
            try {
                append_vertices(last.vertices);
                return true;
            } catch (...) {
                return false;
            }
        }
    }

    try {
        GlesBatch batch{};
        batch.draw = draw;
        append_vertices(batch.vertices);
        s.batches.push_back(std::move(batch));
        return true;
    } catch (...) {
        return false;
    }
}

void ge_gpu_backend_accumulate_color_triangles(
    const GeGpuDrawDescriptor &draw,
    std::span<const GeGpuVertex> triangle_vertices) noexcept {
    GlesState &s = state();
    if (!s.enabled || triangle_vertices.empty()) return;
    if (!append_or_merge_color_batch(s, draw, triangle_vertices))
        ++s.report.rejected_gpu_draws;
}

void ge_gpu_backend_accumulate_hardware_triangles(
    const GeGpuDrawDescriptor &input_draw,
    const GeGpuHardwareTransform &transform,
    std::span<const GeGpuVertex> vertices,
    std::span<const std::uint32_t> triangle_indices) noexcept {
    const GeGpuDrawDescriptor draw = snapshot_texture_draw(input_draw);
    GlesState &s = state();
    if (!s.enabled || vertices.empty()) return;
    try {
        GlesBatch batch{};
        batch.draw = draw;
        batch.hardware_transform = true;
        batch.transform = transform;
        batch.vertices.assign(vertices.begin(), vertices.end());
        batch.indices.assign(triangle_indices.begin(), triangle_indices.end());
        s.batches.push_back(std::move(batch));
        ++s.report.hw_transform_draw_calls;
        s.report.hw_transform_vertices += vertices.size();
    } catch (...) {
        ++s.report.rejected_gpu_draws;
    }
}

bool ge_gpu_backend_accumulate_hardware_packed_0115(
    const GeGpuDrawDescriptor &, const GeGpuHardwareTransform &,
    std::span<const std::byte>, std::uint32_t,
    std::span<const std::uint32_t>) noexcept {
    // The generic decoded hardware-transform path is already GPU-rasterized.
    // Packed direct decode can be added after the first Android GPU bring-up.
    return false;
}

void ge_gpu_backend_set_native_window(void *native_window) noexcept {
    GlesState &s = state();
    auto *incoming = static_cast<ANativeWindow *>(native_window);
    std::lock_guard<std::mutex> guard(s.window_mutex);
    if (incoming == s.window) return;
    if (incoming != nullptr) ANativeWindow_acquire(incoming);
    if (s.window != nullptr) ANativeWindow_release(s.window);
    s.window = incoming;
    s.surface_dirty.store(true, std::memory_order_release);
}

void ge_gpu_backend_set_display_framebuffer(
    std::uint32_t address,
    std::uint32_t logical_width,
    std::uint32_t logical_height) noexcept {
    GlesState &s = state();
    s.display_framebuffer = address & 0x001FFFF0u;
    s.display_logical_width = std::max<std::uint32_t>(1u, logical_width);
    s.display_logical_height = std::max<std::uint32_t>(1u, logical_height);
    android_host::set_source_size(
        s.display_logical_width, s.display_logical_height);

    GlesTarget &target = target_metadata(s, s.display_framebuffer);
    if (target.logical_width != s.display_logical_width ||
        target.logical_height != s.display_logical_height) {
        target.logical_width = s.display_logical_width;
        target.logical_height = s.display_logical_height;
        target.cleared = false;
    }
}

void ge_gpu_backend_display_logical_size(
    std::uint32_t &width, std::uint32_t &height) noexcept {
    const GlesState &s = state();
    width = s.display_logical_width;
    height = s.display_logical_height;
}

bool ge_gpu_backend_finish_color_frame(std::uint64_t vblank) noexcept {
    GlesState &s = state();
    if (!s.enabled) return false;
    const auto perf_started = std::chrono::steady_clock::now();

    std::string error;
    if (!ensure_context(s, error)) {
        s.report.message = error;
        s.direct_present_ok = false;
        return false;
    }

    if (s.frame_epoch <= 12u) {
        runtime_log_line("gles: finish frame=" + std::to_string(s.frame_epoch) +
                         " batches=" + std::to_string(s.batches.size()) +
                         " vblank=" + std::to_string(vblank));
    }

    // Upload all frame geometry once. The old path called glBufferData for
    // every draw, which is extremely expensive on Mali when a scene contains
    // hundreds/thousands of PSP draws.
    s.frame_vertices.clear();
    s.frame_indices.clear();
    std::size_t total_vertices = 0u;
    std::size_t total_indices = 0u;
    for (const GlesBatch &batch : s.batches) {
        total_vertices += batch.vertices.size();
        total_indices += batch.indices.size();
    }
    try {
        s.frame_vertices.reserve(total_vertices);
        s.frame_indices.reserve(total_indices);
        for (GlesBatch &batch : s.batches) {
            batch.first_vertex = static_cast<std::uint32_t>(s.frame_vertices.size());
            batch.first_index = static_cast<std::uint32_t>(s.frame_indices.size());
            s.frame_vertices.insert(
                s.frame_vertices.end(), batch.vertices.begin(), batch.vertices.end());

            if (!batch.indices.empty()) {
                for (std::uint32_t index : batch.indices)
                    s.frame_indices.push_back(index + batch.first_vertex);
            }
        }
    } catch (...) {
        s.report.message = "OpenGL ES frame geometry staging failed";
        return false;
    }

    glBindVertexArray(s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(
                     s.frame_vertices.size() * sizeof(GeGpuVertex)),
                 s.frame_vertices.empty() ? nullptr : s.frame_vertices.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(
                     s.frame_indices.size() * sizeof(std::uint32_t)),
                 s.frame_indices.empty() ? nullptr : s.frame_indices.data(),
                 GL_STREAM_DRAW);

    bool drew = false;
    for (GlesBatch &batch : s.batches) {
        if (batch.vertices.empty()) continue;
        if (!draw_batch(s, batch, error)) {
            if (!error.empty()) runtime_log_error("gles draw", error);
            ++s.report.rejected_gpu_draws;
            continue;
        }
        drew = true;
    }

    bool presented = false;
    auto found = s.targets.find(s.display_framebuffer);
    if (drew && found != s.targets.end() && found->second.color != 0u) {
        if (s.frame_epoch <= 12u) runtime_log_line("gles: presenting frame");
        presented = present_target(s, found->second, error);
        if (!presented && !error.empty())
            runtime_log_error("gles present", error);
    }

    const auto perf_now = std::chrono::steady_clock::now();
    if (s.perf_window == std::chrono::steady_clock::time_point{}) s.perf_window = perf_now;
    ++s.perf_epochs;
    if (presented) ++s.perf_presents;
    const double wall_s = std::chrono::duration<double>(perf_now - s.perf_window).count();
    if (wall_s >= 2.0) {
        runtime_log_line("perf: presentFPS=" + std::to_string(s.perf_presents/wall_s) +
            " presentCalls=" + std::to_string(s.perf_presents) +
            " finishCalls=" + std::to_string(s.perf_epochs) +
            " wallSec=" + std::to_string(wall_s) +
            " colorTestDraws=" + std::to_string(s.perf_color_tests));
        s.perf_presents = s.perf_epochs = s.perf_color_tests = 0;
        s.perf_window = perf_now;
    }
    s.report.game_frame_vblank = vblank;
    if (drew) ++s.report.game_frames;
    s.report.offscreen_width =
        found != s.targets.end() ? found->second.render_width : 0u;
    s.report.offscreen_height =
        found != s.targets.end() ? found->second.render_height : 0u;
    s.report.release_candidate_ready = presented;
    s.report.swapchain_active = s.surface != EGL_NO_SURFACE;
    const std::uint64_t frame_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - perf_started).count());
    s.perf_frame_ns += frame_ns;
    ++s.perf_frame_count;

    if (s.frame_epoch <= 12u || (s.frame_epoch % 120u) == 0u) {
        const double average_ms = s.perf_frame_count != 0u
            ? static_cast<double>(s.perf_frame_ns) /
                static_cast<double>(s.perf_frame_count) / 1.0e6
            : 0.0;
        runtime_log_line(std::string("gles: frame result epoch=") +
                         std::to_string(s.frame_epoch) +
                         " presented=" + (presented ? "1" : "0") +
                         " cpuSubmitMs=" + std::to_string(average_ms) +
                         " targets=" + std::to_string(s.targets.size()) +
                         " textures=" + std::to_string(s.textures.size()) +
                         " cacheMB=" + std::to_string(
                             s.texture_cache_bytes / (1024u * 1024u)) +
                         " draws=" + std::to_string(s.report.game_draw_calls) +
                         " texUploads=" + std::to_string(s.report.decoded_texture_uploads) +
                         " cacheHits=" + std::to_string(s.report.texture_cache_hits) +
                         " evictions=" + std::to_string(s.report.evicted_textures) +
                         " texMB=" + std::to_string(
                             s.report.decoded_texture_bytes / (1024u * 1024u)));
        if ((s.frame_epoch % 120u) == 0u) {
            s.perf_frame_ns = 0u;
            s.perf_frame_count = 0u;
        }
    }

    s.report.message = presented
        ? "OpenGL ES 3 native GE active"
        : (!error.empty() ? error : "OpenGL ES GE frame not ready");

    s.batches.clear();

    // All draw batches for this frame are now complete, so old cache entries
    // can be evicted safely if the configured memory/entry budget was exceeded.
    trim_texture_cache(s, true);
    ++s.frame_epoch;
    return presented;
}

bool ge_gpu_backend_copy_game_frame_rgba(
    std::span<std::byte>) noexcept {
    return false;
}

bool ge_gpu_backend_presents_directly() noexcept {
    const GlesState &s = state();
    // Once EGL owns the Android window, never fall back to ANativeWindow_lock
    // presentation on the same surface. The first GPU frame may still be
    // building, but presentation remains GPU-owned.
    return s.enabled && s.surface != EGL_NO_SURFACE;
}

std::uint32_t ge_gpu_backend_owned_framebuffer() noexcept {
    const GlesState &s = state();
    return s.direct_present_ok ? s.presented_framebuffer : 0u;
}

std::uint32_t ge_gpu_backend_display_framebuffer() noexcept {
    return state().display_framebuffer;
}

std::span<const std::byte> ge_gpu_backend_game_frame_rgba() noexcept {
    return {};
}

bool ge_gpu_backend_copy_offscreen_rgba(
    std::span<std::byte>) noexcept {
    return false;
}

void ge_gpu_backend_mark_window_presented() noexcept {
    state().report.gpu_frame_presented_to_window = true;
}

GeGpuBackendReport ge_gpu_backend_report() {
    return state().report;
}

}  // namespace lcs

#endif
