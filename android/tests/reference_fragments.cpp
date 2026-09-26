// Reference the project's software pixel blender in a separate translation unit.
#include "../../lcs/host/ge_renderer.cpp"
extern "C" std::uint32_t lcs_reference_blend(std::uint32_t src,std::uint32_t dst,
    unsigned eq,unsigned fa,unsigned fb,std::uint32_t ca,std::uint32_t cb,std::uint32_t mask) {
    lcs::FragmentSetup setup{};
    setup.blend_enabled=true;setup.blend_equation=eq;
    setup.blend_source_factor=fa;setup.blend_dest_factor=fb;
    setup.blend_fix_source=lcs::unpack32(ca|0xff000000u);
    setup.blend_fix_dest=lcs::unpack32(cb|0xff000000u);
    const auto out=lcs::blend_pixel(lcs::unpack32(src),lcs::unpack32(dst),setup);
    const auto packed=lcs::pack_gpu_color(out);
    return (packed&~mask)|(dst&mask);
}
