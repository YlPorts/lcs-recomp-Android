#include "lcs_ge_exec.hpp"

#include "ge_renderer.hpp"
#include "lcs_ge_state_policy.hpp"
#if defined(__ANDROID__)
#include "lcs_android_gpu_policy.hpp"
#endif
#include "psprecomp/common.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace lcs {
namespace {

std::array<std::uint32_t, 256> ge_commands{};
GeTransformState ge_transform{};
std::uint32_t ge_base_register = 0u;
std::uint32_t ge_offset_address = 0u;
std::uint32_t ge_vertex_address = 0u;
std::uint32_t ge_index_address = 0u;
GeStateRevisions ge_revisions;
bool ge_finish_seen = false;
std::uint32_t ge_finish_arg = 0u;

constexpr std::uint32_t kMaxCommands = 2'000'000u;

std::uint32_t relative_address(std::uint32_t data24) {
    const std::uint32_t extended = ((ge_base_register & 0x000F0000u) << 8u) | (data24 & 0x00FFFFFFu);
    return (ge_offset_address + extended) & 0x0FFFFFFFu;
}

}  // namespace

void execute_ge_list_rendered(psprecomp::GuestMemory &memory, std::uint32_t list_address) {
    ge_finish_seen = false;
    ge_finish_arg = 0u;
    if (list_address == 0u) return;
    ge_revisions.begin_list();
#if defined(__ANDROID__)
    // New list: revalidate content but keep images pinned by queued draws.
    android_gles_texture_barrier();
#endif

    static const bool diag = std::getenv("LCS_GE_RENDER_DIAG") != nullptr;
    std::uint64_t total_pixels_tested = 0u;
    std::uint64_t total_pixels_written = 0u;
    std::uint64_t total_triangles = 0u;
    struct ReturnPoint { std::uint32_t pc{}, offset{}; };
    std::array<ReturnPoint, 16> call_stack{};
    std::uint32_t call_depth = 0u;
    std::uint32_t pc = list_address;
    bool bbox_visible = false;

    for (std::uint32_t executed = 0u; executed < kMaxCommands; ++executed) {
        if (!memory.contains(pc, 4u)) break;
        const std::uint32_t word = memory.load32(pc);
        const std::uint32_t op_pc = pc;
        pc = (pc + 4u) & 0x0FFFFFFFu;
        const std::uint32_t command = word >> 24u;
        const std::uint32_t data = word & 0x00FFFFFFu;
        // Matrix data consumes cursors even if consecutive words are equal.
        ge_revisions.update(command, ge_commands[command] != data);
        ge_commands[command] = data;

        if (command >= 0x2Au && command <= 0x41u)
            update_ge_transform_state(ge_transform, command, data);

        switch (command) {
        case 0x00u:
            break;
        case 0x01u:
            ge_vertex_address = relative_address(data);
            break;
        case 0x02u:
            ge_index_address = relative_address(data);
            break;
        case 0x10u:
            ge_base_register = data;
            break;
        case 0x13u:
            ge_offset_address = data << 8u;
            break;
        case 0x14u: // ORIGIN uses the address of this command.
            ge_offset_address = op_pc;
            break;
        case 0x07u: { // BBOX consumes the current vertex/index stream.
            GeBoundingBoxResult result{};
            std::string error;
            const auto count = data & 0xFFFFu;
            if (test_ge_bounding_box(memory, ge_commands, ge_transform,
                    ge_vertex_address, ge_index_address, count, result, error)) {
                bbox_visible = result.visible;
                ge_vertex_address = result.next_vertex_address;
                ge_index_address = result.next_index_address;
                record_ge_bbox_result(bbox_visible);
            } else {
                // Invalid guest data must not remove the rest of a visible scene.
                bbox_visible = true;
                record_ge_bbox_result(true);
            }
            break;
        }
        case 0x08u:
            pc = relative_address(data & 0x00FFFFFCu);
            break;
        case 0x09u:
            if (!bbox_visible) {
                const auto target = relative_address(data & 0x00FFFFFCu);
                if (memory.contains(target, 4u)) { pc = target; record_ge_bbox_jump(); }
            }
            break;
        case 0x0Au:
            if (call_depth < call_stack.size()) {
                call_stack[call_depth++] = ReturnPoint{pc, ge_offset_address};
                pc = relative_address(data & 0x00FFFFFCu);
            }
            break;
        case 0x0Bu:
            if (call_depth != 0u) {
                const auto saved = call_stack[--call_depth];
                pc = saved.pc; ge_offset_address = saved.offset;
            }
            break;
        case 0x0Fu:
            ge_finish_seen = true;
            ge_finish_arg = data;
            break;
        case 0x0Cu:
            flush_ge_deferred_rasterization(memory);
            if (diag) {
                static std::uint64_t lists = 0u;
                if ((++lists % 30u) == 0u)
                    std::cerr << "[ge-render] list=" << lists << " tris=" << total_triangles
                              << " tested=" << total_pixels_tested
                              << " written=" << total_pixels_written << "\n";
            }
            return;
        case 0xC4u: { // CLUTLOAD copies bytes into the GE internal palette.
            flush_ge_deferred_rasterization(memory);
            const std::uint32_t address = (ge_commands[0xB0u] & 0x00FFFFF0u) |
                ((ge_commands[0xB1u] << 8u) & 0x0F000000u);
            load_ge_clut(ge_transform.clut, memory, address, data);
#if defined(__ANDROID__)
            android_gles_texture_barrier();
#endif
            break;
        }
        case 0xCBu:  // TEXFLUSH
        case 0xCCu:  // TEXSYNC
#if defined(__ANDROID__)
            android_gles_texture_barrier();
#endif
            break;
        case 0x04u: {
            GeRenderStats stats{};
            std::string error;
            if (!render_ge_primitive(memory, ge_commands, ge_transform, ge_vertex_address,
                                     ge_index_address, data, stats, error, 1u, ge_revisions.draw, ge_revisions.vertex, ge_revisions.lighting,
                                     diag)) {
                if (diag)
                    std::cerr << "[ge-render] primitive failed at " << psprecomp::hex32(op_pc)
                              << ": " << error << "\n";
                flush_ge_deferred_rasterization(memory);
                return;
            }
            ge_vertex_address = stats.next_vertex_address;
            ge_index_address = stats.next_index_address;
            if (diag) {
                total_pixels_tested += stats.pixels_tested;
                total_pixels_written += stats.pixels_written;
                total_triangles += stats.triangles;
            }
            break;
        }
        default:
            break;
        }
    }
    flush_ge_deferred_rasterization(memory);
}

GeListPrescan prescan_ge_list(psprecomp::GuestMemory &memory, std::uint32_t list_address) {
    GeListPrescan result{};
    if (list_address == 0u) return result;
    std::uint32_t base_register = ge_base_register;
    std::uint32_t offset_address = ge_offset_address;
    const auto relative = [&](std::uint32_t data24) {
        const std::uint32_t extended = ((base_register & 0x000F0000u) << 8u) | (data24 & 0x00FFFFFFu);
        return (offset_address + extended) & 0x0FFFFFFFu;
    };
    struct ReturnPoint { std::uint32_t pc{}, offset{}; };
    std::array<ReturnPoint, 16> call_stack{};
    std::uint32_t call_depth = 0u;
    std::uint32_t pc = list_address;
    for (std::uint32_t executed = 0u; executed < kMaxCommands; ++executed) {
        if (!memory.contains(pc, 4u)) break;
        const std::uint32_t word = memory.load32(pc);
        pc = (pc + 4u) & 0x0FFFFFFFu;
        const std::uint32_t command = word >> 24u;
        const std::uint32_t data = word & 0x00FFFFFFu;
        switch (command) {
        case 0x10u: base_register = data; break;
        case 0x13u: offset_address = data << 8u; break;
        case 0x14u: offset_address = (pc - 4u) & 0x0FFFFFFFu; break;
        case 0x08u: pc = relative(data & 0x00FFFFFCu); break;
        case 0x0Au:
            if (call_depth < call_stack.size()) {
                call_stack[call_depth++] = ReturnPoint{pc, offset_address};
                pc = relative(data & 0x00FFFFFCu);
            }
            break;
        case 0x0Bu:
            if (call_depth != 0u) {
                const auto saved = call_stack[--call_depth];
                pc = saved.pc; offset_address = saved.offset;
            }
            break;
        case 0x0Fu:
            result.finished = true;
            result.finish_argument = data;
            break;
        case 0x0Cu:
            return result;
        default:
            break;
        }
    }
    return result;
}

bool rendered_list_finished() { return ge_finish_seen; }
std::uint32_t rendered_finish_argument() { return ge_finish_arg; }

std::uint32_t rendered_render_target() {
    return 0x04000000u | (ge_commands[0x9Cu] & 0x001FFFF0u);
}

std::uint32_t rendered_render_stride() { return ge_commands[0x9Du] & 0x7FCu; }

}
