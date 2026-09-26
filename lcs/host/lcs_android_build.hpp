#pragma once
namespace lcs {
// Native identity, not a launcher label; stamped from the source actually compiled.
const char *android_build_identity() noexcept;
}
