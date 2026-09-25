#include "android_host.hpp"
#include "android_debug.hpp"
#include "psprecomp/runtime.hpp"

#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <csignal>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

int lcs_android_entry(int argc, char **argv);

namespace {

char g_crash_marker_path[768]{};

void native_crash_handler(int signal_number) {
    const char *message = "native signal\n";
    std::size_t message_length = 14u;
    switch (signal_number) {
    case SIGSEGV: message = "SIGSEGV\n"; message_length = 8u; break;
    case SIGABRT: message = "SIGABRT\n"; message_length = 8u; break;
    case SIGBUS:  message = "SIGBUS\n";  message_length = 7u; break;
    case SIGILL:  message = "SIGILL\n";  message_length = 7u; break;
    case SIGFPE:  message = "SIGFPE\n";  message_length = 7u; break;
    default: break;
    }

    if (g_crash_marker_path[0] != '\0') {
        const int fd = open(g_crash_marker_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd >= 0) {
            (void)write(fd, message, message_length);
            (void)close(fd);
        }
    }

    std::signal(signal_number, SIG_DFL);
    std::raise(signal_number);
}

void install_native_crash_handlers(const std::string &config_path) {
    std::string marker = config_path;
    const std::size_t slash = marker.find_last_of('/');
    if (slash != std::string::npos) marker.resize(slash + 1u);
    else marker.clear();
    marker += "native_crash.txt";

    std::memset(g_crash_marker_path, 0, sizeof(g_crash_marker_path));
    const std::size_t count = std::min(marker.size(), sizeof(g_crash_marker_path) - 1u);
    std::memcpy(g_crash_marker_path, marker.data(), count);
    if (g_crash_marker_path[0] != '\0') (void)unlink(g_crash_marker_path);

    struct sigaction action {};
    action.sa_handler = native_crash_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    (void)sigaction(SIGSEGV, &action, nullptr);
    (void)sigaction(SIGABRT, &action, nullptr);
    (void)sigaction(SIGBUS,  &action, nullptr);
    (void)sigaction(SIGILL,  &action, nullptr);
    (void)sigaction(SIGFPE,  &action, nullptr);
}

std::string from_jstring(JNIEnv *env, jstring text) {
    if (text == nullptr) return {};
    const char *chars = env->GetStringUTFChars(text, nullptr);
    if (chars == nullptr) return {};
    std::string value(chars);
    env->ReleaseStringUTFChars(text, chars);
    return value;
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_lcsrecomp_MainActivity_nativeSetSurface(
    JNIEnv *env, jclass, jobject surface) {
    ANativeWindow *window = surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;
    lcs::android_host::set_window(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_lcsrecomp_MainActivity_nativeSetDisplaySize(
    JNIEnv *, jclass, jint width, jint height) {
    if (width > 0 && height > 0)
        lcs::android_host::set_display_size(
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_lcsrecomp_MainActivity_nativeSetInput(
    JNIEnv *, jclass, jint buttons, jint analog_x, jint analog_y,
    jint camera_x, jint camera_y, jboolean accelerate, jboolean brake) {
    lcs::android_host::set_input(
        static_cast<std::uint32_t>(buttons),
        static_cast<std::uint8_t>(analog_x),
        static_cast<std::uint8_t>(analog_y),
        static_cast<int>(camera_x),
        static_cast<int>(camera_y),
        accelerate == JNI_TRUE,
        brake == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_lcsrecomp_MainActivity_nativeRequestStop(
    JNIEnv *, jclass) {
    lcs::android_host::request_stop();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ylports_lcsrecomp_MainActivity_nativeGetDebugStatus(
    JNIEnv *env, jclass) {
    const std::string status = lcs::android_debug::status_text(
        psprecomp::runtime_dispatch_pc(),
        psprecomp::runtime_thread_uid(),
        psprecomp::runtime_thread_name());
    return env->NewStringUTF(status.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ylports_lcsrecomp_MainActivity_nativeRun(
    JNIEnv *env, jclass, jstring game_root_text, jstring config_path_text) {
    const std::string game_root = from_jstring(env, game_root_text);
    const std::string config_path = from_jstring(env, config_path_text);
    if (game_root.empty()) return 2;

    install_native_crash_handlers(config_path);

    lcs::android_debug::reset();
    lcs::android_debug::set_stage(lcs::android_debug::Stage::NativeEntry);

    if (!config_path.empty())
        setenv("PSPRECOMP_CONFIG", config_path.c_str(), 1);

    // Android GPU path: PSP GE commands are translated to OpenGL ES 3.
    // Software rasterization is disabled once the mobile GPU backend owns the
    // framebuffer, which removes the main ~5 FPS CPU bottleneck.
    setenv("PSPRECOMP_GE_BACKEND", "gles", 1);
    setenv("PSPRECOMP_GE_GPU_SKIP_SOFTWARE_RASTER", "1", 1);
    setenv("PSPRECOMP_GE_GPU_HW_TRANSFORM", "1", 1);
    setenv("PSPRECOMP_GE_DIRECT_NONINDEXED_DRAW", "1", 1);
    setenv("PSPRECOMP_GLES_SCALE", "1", 1);
    setenv("LCS_FPS_CAP", "60", 1);
    setenv("PSPRECOMP_INTERNAL_WIDTH", "480", 1);
    setenv("PSPRECOMP_INTERNAL_HEIGHT", "272", 1);
    setenv("PSPRECOMP_AUDIO", "1", 1);

    // FFmpeg/PMF decoding is not linked in the Android bootstrap yet. Tell the
    // HLE to reject MPEG creation immediately so startup movies are skipped
    // instead of repeatedly waiting on a decoder that can never produce a frame.
    setenv("LCS_SKIP_MPEG", "1", 1);

    // EGL contexts are thread-affine. The first GLES build let the GE worker
    // accumulate/upload on one thread while vblank presentation used another,
    // racing the same context, texture maps and batch vectors. Keep the GPU
    // renderer on the native game thread; rasterization still happens on the
    // GPU, so this does NOT restore the old CPU pixel bottleneck.
    setenv("LCS_GE_ASYNC", "0", 1);

    // Mobile software-raster tuning for the 2+6 core layout common on midrange
    // ARM phones. Six row workers plus the GE/game threads keeps all cores busy
    // without creating excessive little-core contention.
    setenv("PSPRECOMP_RASTER_THREADS", "6", 1);
    setenv("PSPRECOMP_RASTER_PARALLEL_PIXELS", "512", 1);
    setenv("PSPRECOMP_GE_PARALLEL_VERTEX_DECODE", "1", 1);
    setenv("PSPRECOMP_GE_PARALLEL_VERTEX_THRESHOLD", "96", 1);
    setenv("PSPRECOMP_GE_PARALLEL_VERTEX_MAX_WORKERS", "6", 1);
    setenv("PSPRECOMP_GE_PARALLEL_TEXTURE_DECODE", "1", 1);

    // Let the renderer choose the actual GE render target. The framebuffer
    // selected through sceDisplaySetFrameBuf can remain black while the game is
    // still rendering into another VRAM target before the flip.
    unsetenv("LCS_NO_PRESENT_RENDER_TARGET");

    std::string executable = game_root + "/LCSNative";
    char *argv[] = {
        executable.data(),
        const_cast<char *>("--game"),
        const_cast<char *>(game_root.c_str())
    };

    const int result = lcs_android_entry(3, argv);
    lcs::android_debug::set_result(result);
    lcs::android_debug::set_stage(lcs::android_debug::Stage::Finished);
    return result;
}
