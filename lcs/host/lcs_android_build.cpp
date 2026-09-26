#include "lcs_android_build.hpp"
#ifndef LCS_ANDROID_SOURCE_SHA
#define LCS_ANDROID_SOURCE_SHA "unversioned"
#endif
namespace lcs {
const char *android_build_identity() noexcept {
    return "LCS Android 0.6.16 source=" LCS_ANDROID_SOURCE_SHA;
}
}
