#include "android_host.hpp"
#include "android_debug.hpp"
#include "psprecomp/runtime.hpp"

#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdlib>
#include <string>

int lcs_android_entry(int argc, char **argv);

namespace {

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

    lcs::android_debug::reset();
    lcs::android_debug::set_stage(lcs::android_debug::Stage::NativeEntry);

    if (!config_path.empty())
        setenv("PSPRECOMP_CONFIG", config_path.c_str(), 1);

    // Android bring-up currently uses the CPU/software GE path.
    setenv("PSPRECOMP_GE_BACKEND", "software", 1);
    setenv("PSPRECOMP_INTERNAL_WIDTH", "480", 1);
    setenv("PSPRECOMP_INTERNAL_HEIGHT", "272", 1);
    setenv("PSPRECOMP_AUDIO", "0", 1);

    // Keep rendering deterministic on mobile while the native Android backend
    // is being validated.  The software GE and presentation both touch PSP
    // framebuffer memory; synchronous execution avoids racing guest writes.
    setenv("LCS_GE_ASYNC", "0", 1);

    // On Android prefer the framebuffer explicitly selected through
    // sceDisplaySetFrameBuf.  The desktop path may prefer the most recent GE
    // render target, which can be a non-displayed intermediate target.
    setenv("LCS_NO_PRESENT_RENDER_TARGET", "1", 1);

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
