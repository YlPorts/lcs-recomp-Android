// Exercise the actual GE decoder with PSP's 16-byte texture row alignment.
#ifndef LCS_TEST_RENDERER
#define LCS_TEST_RENDERER "../../lcs/host/ge_renderer.cpp"
#endif
#include LCS_TEST_RENDERER
#include <iostream>

namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return 0; }
const char *runtime_thread_name() noexcept { return "texture-stride-test"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}

using namespace lcs;
int main() {
    psprecomp::GuestMemory memory;
    constexpr std::uint32_t tex = 0x08020000, pal = 0x08010000;
    std::array<std::uint32_t, 256> commands{};
    commands[0xC5] = 3u | (255u << 8u);
    // The palette deliberately makes wrongly addressed padding visibly green.
    memory.store32(pal, 0xff00ff00u);
    memory.store32(pal + 4, 0xff0000ffu);
    GeClutState palette;
    load_ge_clut(palette, memory, pal, 1);
    unsigned failures = 0;
    auto check = [&](bool ok, const char *name) {
        std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
        failures += !ok;
    };
    auto red = [](const Color &color) {
        return color.r == 255 && color.g == 0 && color.b == 0 && color.a == 255;
    };

    // Eight-pixel T4 mip: row 1 starts at byte 16, not at byte 4.
    commands[0xC3] = 4;
    commands[0xB9] = 3u | (1u << 8u);
    commands[0xA1] = tex & 0xffffff;
    commands[0xA9] = 8u | ((tex >> 8u) & 0xf0000);
    memory.store8(tex + 16, 0x11);
    auto mip = make_texture_setup_for_level(memory, commands, 1, &palette);
    check(red(sample_texture_nearest(memory, mip, 0, 1)), "T4 small mip reads row 1 at 16-byte boundary instead of green padding");
    auto old_hash = texture_source_signature(memory, mip);
    memory.store8(tex + 16, 0x10);
    check(old_hash != texture_source_signature(memory, mip), "small mip cache signature covers the actual second row");
    memory.store8(tex + 16, 0x11);

    // Width 17 means 16-byte stride after downalignment, not 17/32 bytes.
    // Row 8 is at byte 128, not at byte 256, in the swizzled layout.
    commands[0xC3] = 5;
    commands[0xC2] = 1;
    commands[0xB8] = 4u | (4u << 8u);
    commands[0xA0] = tex & 0xffffff;
    commands[0xA8] = 17u | ((tex >> 8u) & 0xf0000);
    memory.store8(tex + 128, 1);
    auto swizzled = make_texture_setup_for_level(memory, commands, 0, &palette);
    check(red(sample_texture_nearest(memory, swizzled, 0, 8)), "swizzled T8 downaligns row pitch before addressing next eight-row block");

    // Format-dependent minimum/alignment also applies to direct colors and
    // indexed 16-/32-bit data. Stride register upper bits remain masked away.
    constexpr std::array<unsigned, 8> minimum{8, 8, 8, 4, 32, 16, 8, 4};
    bool formats_ok = true;
    for (unsigned format = 0; format < minimum.size(); ++format) {
        commands[0xC3] = format;
        commands[0xC2] = 0;
        for (unsigned width : {0u, 1u, minimum[format] - 1u,
                               minimum[format], minimum[format] + 1u, 2048u}) {
            commands[0xA8] = width | ((tex >> 8u) & 0xf0000);
            auto texture = make_texture_setup_for_level(memory, commands, 0, &palette);
            formats_ok &= texture.buffer_width == minimum[format];
        }
    }
    check(formats_ok, "all eight direct/indexed formats honor minimum, downalignment and 2048 wrap");
    return failures == 0 ? 0 : 1;
}
