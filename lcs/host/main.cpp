#include "psprecomp/common.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"
#include "lcs_profile.hpp"
#include "display_window.hpp"
#include "lcs_audio_output.hpp"
#include "ge_gpu_backend.hpp"
#include "ge_renderer.hpp"
#include "lcs_render_config.hpp"
#include "lcs_runtime_log.hpp"

#if defined(__ANDROID__)
#include "android_debug.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>

#ifndef LCS_NO_GENERATED
namespace psprecomp {
void register_generated_functions(Runtime &runtime);
} // namespace psprecomp
#endif

int main(int argc, char **argv) {
#if defined(__ANDROID__)
    lcs::android_debug::set_stage(lcs::android_debug::Stage::MainStarted);
#endif
    std::filesystem::path elf_path = "game/EBOOT.ELF";
    std::filesystem::path game_root = "game";
    std::uint64_t max_dispatches = std::numeric_limits<std::uint64_t>::max();
    double max_seconds = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--game" && i + 1 < argc) {
            game_root = argv[++i];
            elf_path = game_root / "EBOOT.ELF";
        } else if (arg == "--elf" && i + 1 < argc) {
            elf_path = argv[++i];
        } else if ((arg == "--max-dispatches" || arg == "--dispatch-cap") && i + 1 < argc) {
            max_dispatches = std::strtoull(argv[++i], nullptr, 0);
        } else if (arg == "--max-seconds" && i + 1 < argc) {
            max_seconds = std::strtod(argv[++i], nullptr);
        } else {
            std::cerr << "Ignoring unrecognised argument \"" << arg << "\"\n";
        }
    }

    try {
        std::error_code executable_error;
        const std::filesystem::path executable_directory =
            argc > 0 ? std::filesystem::absolute(argv[0], executable_error).parent_path()
                     : std::filesystem::current_path();
        lcs::initialize_lcs_render_configuration(executable_directory);
        lcs::runtime_log_initialize(lcs::lcs_render_configuration());
        lcs::runtime_log_line("main: configuration initialized");

        auto elf = psprecomp::Elf32Image::from_file(elf_path);
#if defined(__ANDROID__)
        lcs::android_debug::set_stage(lcs::android_debug::Stage::ElfLoaded);
#endif

        psprecomp::Runtime runtime;
        runtime.set_game_root(game_root);
#if defined(__ANDROID__)
        lcs::android_debug::set_stage(lcs::android_debug::Stage::RuntimeReady);
#endif

        const auto stats = elf.load_and_relocate(runtime.memory(), psprecomp::kDefaultPspUserLoadBase);
        std::cout << "Loaded " << elf_path.string() << " (" << stats.total << " relocations applied)\n" << std::flush;

#ifndef LCS_NO_GENERATED
        psprecomp::register_generated_functions(runtime);
#endif
        std::cout << "Registered " << runtime.function_count() << " generated functions\n" << std::flush;


        // User memory arena starts right after the ELF image.
        std::uint64_t image_end = 0u;
        for (std::size_t index = 0; index < elf.segments().size(); ++index) {
            const auto &segment = elf.segments()[index];
            if (segment.type != 1u) continue;  // PT_LOAD
            const std::uint64_t start = elf.segment_runtime_address(index, psprecomp::kDefaultPspUserLoadBase);
            image_end = std::max(image_end, start + segment.memory_size);
        }
        const std::uint32_t user_arena_start =
            static_cast<std::uint32_t>((image_end + 0xFFu) & ~0xFFull);
        lcs::display_window_init();
        lcs::install_profile(runtime, user_arena_start);
#if defined(__ANDROID__)
        lcs::android_debug::set_stage(lcs::android_debug::Stage::ProfileInstalled);
#endif

        lcs::runtime_log_line("main: initializing GE GPU backend");
        std::string gpu_backend_error;
        if (!lcs::initialize_ge_gpu_backend(gpu_backend_error))
            std::cerr << "[ge] GPU backend unavailable: " << gpu_backend_error << "\n";
        lcs::runtime_log_line("main: GE backend init returned");
        lcs::display_window_attach_gpu_backend();
        lcs::runtime_log_line("main: native window attached to GE backend");
        const lcs::GeGpuBackendReport gpu_start = lcs::ge_gpu_backend_report();
        std::cerr << "[ge] backend requested=" << lcs::ge_gpu_backend_name(gpu_start.requested)
                  << " active=" << lcs::ge_gpu_backend_name(gpu_start.active)
                  << " status=" << gpu_start.message << "\n";
        lcs::set_wall_clock_limit(max_seconds);

        if (const auto module = elf.find_module_info(runtime.memory(), psprecomp::kDefaultPspUserLoadBase)) {
            runtime.cpu().set_gpr(28, module->gp);
        } else {
            throw psprecomp::Error("PSP module info not found after relocation");
        }
        runtime.cpu().set_gpr(31, 0u);
        runtime.cpu().set_gpr(4, 0u);
        runtime.cpu().set_gpr(5, 0u);

        const std::uint32_t entry = elf.runtime_entry(psprecomp::kDefaultPspUserLoadBase);
        std::cout << "Running from entry " << psprecomp::hex32(entry) << "\n" << std::flush;
#if defined(__ANDROID__)
        lcs::android_debug::set_stage(lcs::android_debug::Stage::Running);
#endif
        lcs::runtime_log_line("main: entering recompiled game");
        runtime.run(entry, max_dispatches);
        lcs::runtime_log_line("main: runtime.run returned");
        lcs::ge_worker_shutdown();

        if (runtime.stopped()) {
            std::cout << "Runtime stopped: " << runtime.stop_reason() << "\n";
            lcs::debug_dump_threads();
            lcs::dump_pc_profile();
            lcs::dump_framebuffer_stats(runtime);
            lcs::dump_watched_memory(runtime);
            lcs::dump_disc_read_stats();
        }
        if (std::getenv("PSPRECOMP_GE_PHASE_DIAG") != nullptr) {
            const lcs::GePhaseTotals phases = lcs::ge_phase_totals();
            std::cerr << "[ge-phase] pixel_loop_ms=" << phases.pixel_loop_ns / 1000000u
                      << " draw_setup_ms=" << phases.draw_setup_ns / 1000000u
                      << " texture_upload_ms=" << phases.texture_upload_ns / 1000000u
                      << " vertex_decode_ms=" << phases.vertex_decode_ns / 1000000u
                      << " triangle_prep_ms=" << phases.triangle_prep_ns / 1000000u
                      << " triangles=" << phases.triangles
                      << " primitives=" << phases.primitives
                      << " vertices=" << phases.vertices << "\n";
        }
        runtime.report_hle_histogram(60u);
        const lcs::GeGpuBackendReport gpu_end = lcs::ge_gpu_backend_report();
        std::cerr << "[ge-gpu] frames=" << gpu_end.game_frames
                  << " draws=" << gpu_end.game_draw_calls
                  << " tris=" << gpu_end.game_triangles
                  << " verts=" << gpu_end.game_vertices
                  << " hw_transform_draws=" << gpu_end.hw_transform_draw_calls
                  << " tex_uploads=" << gpu_end.decoded_texture_uploads
                  << " tex_hits=" << gpu_end.texture_cache_hits
                  << " rejected=" << gpu_end.rejected_gpu_draws
                  << " no_texture=" << gpu_end.game_textured_draws_without_texture
                  << " vertex_overflows=" << gpu_end.game_vertex_overflows
                  << " no_display_target=" << gpu_end.frames_without_displayed_target
                  << " presented=" << gpu_end.gpu_frame_presented_to_window << "\n";
        lcs::audio_output_shutdown();
    } catch (const std::exception &ex) {
        std::cerr << "LCSNative error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
