#include "android_host.hpp"

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

extern "C" JNIEXPORT jint JNICALL
Java_com_ylports_lcsrecomp_MainActivity_nativeRun(
    JNIEnv *env, jclass, jstring game_root_text, jstring config_path_text) {
    const std::string game_root = from_jstring(env, game_root_text);
    const std::string config_path = from_jstring(env, config_path_text);
    if (game_root.empty()) return 2;

    if (!config_path.empty())
        setenv("PSPRECOMP_CONFIG", config_path.c_str(), 1);

    // Android v0.1 deliberately uses the existing CPU/software GE path.
    // A Vulkan GE backend can be added without touching the recompiled game.
    setenv("PSPRECOMP_GE_BACKEND", "software", 1);
    setenv("PSPRECOMP_INTERNAL_WIDTH", "480", 1);
    setenv("PSPRECOMP_INTERNAL_HEIGHT", "272", 1);
    setenv("PSPRECOMP_AUDIO", "0", 1);

    std::string executable = game_root + "/LCSNative";
    char *argv[] = {
        executable.data(),
        const_cast<char *>("--game"),
        const_cast<char *>(game_root.c_str())
    };
    return lcs_android_entry(3, argv);
}
