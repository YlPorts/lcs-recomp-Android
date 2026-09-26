#!/usr/bin/env python3
"""One-time 0.6.8 -> 0.6.9 source migration; validate all anchors before writing.
The workflow tests and commits the resulting readable source files.
"""
from pathlib import Path
import sys
ROOT=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
changed={}
def one(s,a,b):
    if s.count(a)!=1: raise RuntimeError(f'Expected one anchor, got {s.count(a)}: {a[:100]!r}')
    return s.replace(a,b,1)
def edit(path,fn):
    s=(ROOT/path).read_text(); n=fn(s)
    if n==s: raise RuntimeError('No changes: '+path)
    changed[path]=n

def audio(s):
    s=one(s,'    std::uint64_t resyncs{};','    std::uint64_t resyncs{};\n    std::uint64_t starvation_rebases{};\n    std::atomic<std::uint64_t> mixed_samples{0};\n    std::atomic<std::uint64_t> invalidated_samples{0};')
    s=one(s,'        std::uint64_t late=0, contributed=0;','        std::uint64_t late=0, contributed=0, invalidated=0;')
    s=one(s,'c.generation.load(std::memory_order_acquire),late);','c.generation.load(std::memory_order_acquire),late, &invalidated);')
    s=one(s,'        s.late.fetch_add(late,std::memory_order_relaxed);','        s.late.fetch_add(late,std::memory_order_relaxed);\n        s.mixed_samples.fetch_add(contributed,std::memory_order_relaxed);\n        s.invalidated_samples.fetch_add(invalidated,std::memory_order_relaxed);')
    s=one(s,'    s.anchored=false; s.produced=s.dropped=s.resyncs=0;','    s.anchored=false; s.produced=s.dropped=s.resyncs=s.starvation_rebases=0;\n    s.mixed_samples.store(0); s.invalidated_samples.store(0);')
    a=s.index('    const bool changed=c.active &&');b=s.index('    c.expected_guest=',a)
    s=s[:a]+'''    // Small guest-time gaps are scheduling jitter, not channel replacement.
    // Changing generation here cancelled PCM before its playback lead elapsed.
    const bool format_changed=c.active && (c.rate!=rate || c.stereo!=stereo);
    const bool timeline_jump=c.active && gap>kAndroidRate/2;
    if (!c.active) {
        const auto scheduled=guest>=s.guest_anchor
            ? s.host_anchor+guest-s.guest_anchor : read+kAndroidLead;
        c.cursor=std::max(scheduled,read+kAndroidLead);
        if (c.cursor>read+kAndroidRate) c.cursor=read+kAndroidLead;
        c.rate=rate; c.stereo=stereo; c.active=true;
        c.resampler.reset(rate,stereo);
    } else {
        if (format_changed || timeline_jump) {
            // Queued PCM is already in output format. Preserve it and reset
            // only interpolation history. Explicit channel reset still cancels.
            ++s.resyncs;
            c.resampler.reset(rate,stereo);
            c.rate=rate; c.stereo=stereo;
        }
        constexpr std::uint64_t callback_safety=512;
        if (c.cursor<read+callback_safety) {
            // Recover a real underrun without cancelling pending packets or
            // restarting another full 93ms silent prebuffer.
            c.cursor=read+callback_safety;
            ++s.starvation_rebases;
        }
    }
'''+s[b:]
    return one(s,'        " dropped="+std::to_string(s.dropped)+" resyncs="+std::to_string(s.resyncs)+','''        " dropped="+std::to_string(s.dropped)+" resyncs="+std::to_string(s.resyncs)+
        " starvationRebases="+std::to_string(s.starvation_rebases)+
        " mixedSamples="+std::to_string(s.mixed_samples.load())+
        " invalidatedSamples="+std::to_string(s.invalidated_samples.load())+''')

def queue(s):
    s=one(s,'std::uint32_t generation, std::uint64_t &late) noexcept {','std::uint32_t generation, std::uint64_t &late,\n                      std::uint64_t *invalidated = nullptr) noexcept {')
    return one(s,'            if (p.generation != generation || offset_ >= p.frames) {','''            if (p.generation != generation || offset_ >= p.frames) {
                if (invalidated && p.generation != generation && p.frames > offset_)
                    *invalidated += p.frames - offset_;''')

def renderer(s):
    s=one(s,'void record_screen_vertex(GeRenderStats &stats, const Vertex &vertex) noexcept {','void record_screen_vertex(GeRenderStats &stats, const Vertex &vertex) noexcept {\n    if (!g_collect_ge_render_stats) return;')
    for name in ('decode_vertex','decode_vertex_optimized'):
        a=s.index('bool '+name+'(');b=s.index('\n}',a)+2;body=s[a:b]
        body=one(body,'Vertex &vertex, std::string &error)','Vertex &vertex, std::string &error,\n                   const PreparedLighting *prepared = nullptr)')
        if name=='decode_vertex':
            body=one(body,'        vertex.color = apply_lighting(vertex.color, layout.color_type >= 4u, world_position,\n                                      world_normal, commands);','''        vertex.color = prepared
            ? apply_prepared_lighting(vertex.color, world_position, world_normal, *prepared)
            : apply_lighting(vertex.color, layout.color_type >= 4u, world_position,
                             world_normal, commands);''')
        else:
            body=one(body,'return decode_vertex(memory, address, layout, commands, transform, vertex, error);','return decode_vertex(memory, address, layout, commands, transform, vertex, error, prepared);')
        s=s[:a]+body+s[b:]
    s=one(s,'        const bool reuse_indexed = gpu_backend_enabled && layout.index_type != 0u;\n        for (std::uint32_t i = 0u; i < count; ++i) {','''        const bool reuse_indexed = gpu_backend_enabled && layout.index_type != 0u;
        // Material/light state is identical for all vertices in this primitive.
        const PreparedLighting primitive_lighting = prepare_lighting(layout.color_type >= 4u, commands);
        for (std::uint32_t i = 0u; i < count; ++i) {''')
    s=one(s,'            if (!decode_vertex_optimized(memory, vertex_address + index * layout.stride, layout,\n                                         commands, transform, vertex, error)) return false;','            if (!decode_vertex_optimized(memory, vertex_address + index * layout.stride, layout,\n                                         commands, transform, vertex, error, &primitive_lighting)) return false;')
    return one(s,'    const bool depth_clip_enabled = (data24(commands[0x1Cu]) & 1u) != 0u;\n    static thread_local ClipPolygon polygon;\n    clip_triangle(a, b, c, depth_clip_enabled, polygon);','''    const bool depth_clip_enabled = (data24(commands[0x1Cu]) & 1u) != 0u;
    // Preserve the original polygon clipper for boundary-crossing triangles.
    const auto outcode = [&](const Vertex &v) {
        unsigned bits = 0;
        for (unsigned p = 0; p < (depth_clip_enabled ? 6u : 4u); ++p)
            if (!(clip_distance(v, p) >= 0.0f)) bits |= 1u << p;
        return bits;
    };
    const unsigned oa=outcode(a), ob=outcode(b), oc=outcode(c);
    if ((oa & ob & oc) != 0u) return;
    if ((oa | ob | oc) == 0u) {
        Vertex va=a, vb=b, vc=c;
        if (viewport_transform(va, commands) && viewport_transform(vb, commands) &&
            viewport_transform(vc, commands)) emplace_prepared(va,vb,vc);
        return;
    }
    static thread_local ClipPolygon polygon;
    clip_triangle(a, b, c, depth_clip_enabled, polygon);''')

def executor(s):
    s=one(s,'#include "ge_renderer.hpp"','#include "ge_renderer.hpp"\n#if defined(__ANDROID__)\n#include "lcs_android_gpu_policy.hpp"\n#endif')
    s=one(s,'bool ge_finish_seen = false;','std::uint64_t ge_state_revision = 1u;\nbool ge_finish_seen = false;')
    s=one(s,'    if (list_address == 0u) return;','''    if (list_address == 0u) return;
#if defined(__ANDROID__)
    // New list: revalidate content but keep images pinned by queued draws.
    android_gles_texture_barrier();
#endif''')
    s=one(s,'        ge_commands[command] = data;','''        // Matrix data consumes cursors even if consecutive words are equal.
        if (command >= 0x12u && (ge_commands[command] != data ||
            (command >= 0x2Au && command <= 0x41u))) ++ge_state_revision;
        ge_commands[command] = data;''')
    s=one(s,'error, 1u, 0u, 0u, 0u,','error, 1u, ge_state_revision, ge_state_revision, ge_state_revision,')
    return one(s,'        case 0x04u: {','''        case 0xC4u:  // CLUTLOAD
        case 0xCBu:  // TEXFLUSH
        case 0xCCu:  // TEXSYNC
#if defined(__ANDROID__)
            android_gles_texture_barrier();
#endif
            break;
        case 0x04u: {''')

def gles(s):
    s=one(s,'    std::uint64_t frame_epoch{1u};','''    std::uint64_t frame_epoch{1u};
    std::uint64_t validation_epoch{1u};
    std::unordered_map<std::uint32_t, GLuint> samplers;
    std::array<std::uint32_t,16> pixel_uniform_key{};
    bool pixel_uniform_valid{};
    bool cpu_transform_uniform_valid{};
    std::uint32_t uniform_width{}, uniform_height{};''')
    s=one(s,'    s.texture_versions.clear();','''    s.texture_versions.clear();
    for (auto [key, sampler] : s.samplers) { (void)key; glDeleteSamplers(1,&sampler); }
    s.samplers.clear();
    s.pixel_uniform_valid=false;
    s.cpu_transform_uniform_valid=false;''')
    s=one(s,'s.texture_versions.observe(texture_key(draw), draw.texture_content_signature, s.frame_epoch);','s.texture_versions.observe(texture_key(draw), draw.texture_content_signature, s.validation_epoch);')
    s=one(s,'s.texture_versions.needs_check(texture_key(draw), s.frame_epoch);','s.texture_versions.needs_check(texture_key(draw), s.validation_epoch);')
    s=one(s,'bool initialize_ge_gpu_backend(std::string &error) {','void android_gles_texture_barrier() noexcept {\n    ++state().validation_epoch;\n}\n\nbool initialize_ge_gpu_backend(std::string &error) {')
    s=one(s,'    ++s.frame_epoch;','    ++s.frame_epoch;\n    ++s.validation_epoch;')
    s=one(s,'void set_pixel_uniforms(GlesState &s, const GeGpuDrawDescriptor &draw,\n                        bool textured) {\n    glUniform1i(s.u_tex, 0);','''void set_pixel_uniforms(GlesState &s, const GeGpuDrawDescriptor &draw,
                        bool textured) {
    const bool flip=draw.texture_enabled && feedback_target(s, draw.texture_address)!=s.targets.end();
    const std::array<std::uint32_t,16> key{{
        textured, draw.texture_function & 7u, draw.texture_use_alpha,
        draw.texture_double_color, draw.texture_env, draw.alpha_test_enabled,
        draw.alpha_function & 7u, draw.alpha_reference & 255u, draw.alpha_mask & 255u,
        draw.fog_enabled, draw.fog_color, draw.framebuffer_format & 3u,
        draw.color_test_enabled && !draw.clear_mode ? (draw.color_test_function & 3u)+1u : 0u,
        draw.color_test_reference, draw.color_test_mask, flip}};
    if (s.pixel_uniform_valid && key==s.pixel_uniform_key) return;
    s.pixel_uniform_key=key; s.pixel_uniform_valid=true;
    glUniform1i(s.u_tex, 0);''')
    s=one(s,'    glUniform2f(s.u_logical_size,\n                static_cast<float>(logical_width),','''    if (!batch.hardware_transform && s.cpu_transform_uniform_valid &&
        s.uniform_width==logical_width && s.uniform_height==logical_height) return;
    s.cpu_transform_uniform_valid=!batch.hardware_transform;
    s.uniform_width=logical_width; s.uniform_height=logical_height;
    glUniform2f(s.u_logical_size,
                static_cast<float>(logical_width),''')
    marker='bool draw_batch(GlesState &s, GlesBatch &batch, std::string &error) {'
    s=one(s,marker,'''void bind_draw_sampler(GlesState &s, const GeGpuDrawDescriptor &draw, GLuint texture) {
    const auto key=static_cast<std::uint32_t>(draw.texture_clamp_u) |
        (static_cast<std::uint32_t>(draw.texture_clamp_v)<<1u) |
        (static_cast<std::uint32_t>(draw.texture_min_linear)<<2u) |
        (static_cast<std::uint32_t>(draw.texture_mag_linear)<<3u) |
        (static_cast<std::uint32_t>(draw.texture_mipmap_enabled && draw.texture_max_level>0u)<<4u) |
        (static_cast<std::uint32_t>(draw.texture_mipmap_linear)<<5u);
    auto [it, inserted]=s.samplers.try_emplace(key,0u);
    if (inserted) {
        glGenSamplers(1,&it->second);
        const GLuint id=it->second;
        glSamplerParameteri(id,GL_TEXTURE_WRAP_S,draw.texture_clamp_u?GL_CLAMP_TO_EDGE:GL_REPEAT);
        glSamplerParameteri(id,GL_TEXTURE_WRAP_T,draw.texture_clamp_v?GL_CLAMP_TO_EDGE:GL_REPEAT);
        GLint filter=draw.texture_min_linear?GL_LINEAR:GL_NEAREST;
        if ((key & 16u)!=0u) filter=draw.texture_mipmap_linear
            ? (draw.texture_min_linear?GL_LINEAR_MIPMAP_LINEAR:GL_NEAREST_MIPMAP_LINEAR)
            : (draw.texture_min_linear?GL_LINEAR_MIPMAP_NEAREST:GL_NEAREST_MIPMAP_NEAREST);
        glSamplerParameteri(id,GL_TEXTURE_MIN_FILTER,filter);
        glSamplerParameteri(id,GL_TEXTURE_MAG_FILTER,draw.texture_mag_linear?GL_LINEAR:GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D,texture);
    glBindSampler(0,it->second);
}

'''+marker)
    s=one(s,'    if (textured) configure_texture_sampling(batch.draw, texture);','    if (textured) bind_draw_sampler(s, batch.draw, texture);')
    return one(s,'    glUseProgram(s.present_program);','    glUseProgram(s.present_program);\n    glBindSampler(0,0);')

def pipeline(s):
    s=one(s,'#include "android_debug.hpp"','#include "android_debug.hpp"\n#include "ge_renderer.hpp"\n#include "lcs_runtime_log.hpp"')
    marker='void check_wall_clock_limit(psprecomp::Runtime &runtime, std::uint64_t sample_mask) {'
    s=one(s,marker,'''#if defined(__ANDROID__)
void android_pipeline_report() {
    static auto started=std::chrono::steady_clock::now();
    static GePhaseTotals previous{};
    static std::uint64_t before_ge{}, before_gpu{}, before_wait{}, frames{};
    ++frames;
    const auto now=std::chrono::steady_clock::now();
    const double elapsed_ms=std::chrono::duration<double,std::milli>(now-started).count();
    if (elapsed_ms<2000.0) return;
    const auto phase=ge_phase_totals();
    const auto ge=g_speed_ge_list_ns.load(), gpu=g_speed_gpu_finish_ns.load(), wait=g_speed_throttle_ns.load();
    const double denominator=1.0e6*static_cast<double>(frames);
    const auto ms=[&](std::uint64_t after,std::uint64_t before) {
        return after>=before ? (after-before)/denominator : 0.0;
    };
    const double ge_ms=ms(ge,before_ge),gpu_ms=ms(gpu,before_gpu),wait_ms=ms(wait,before_wait);
    std::ostringstream report;
    report<<std::fixed<<std::setprecision(3)
        <<"pipeline: frameCalls="<<frames<<" wallMsPerCall="<<elapsed_ms/frames
        <<" geTotalMs="<<ge_ms<<" submitMs="<<gpu_ms<<" waitMs="<<wait_ms
        <<" otherMs="<<std::max(0.0,elapsed_ms/frames-ge_ms-gpu_ms-wait_ms)
        <<" setupMs="<<ms(phase.draw_setup_ns,previous.draw_setup_ns)
        <<" textureMs="<<ms(phase.texture_upload_ns,previous.texture_upload_ns)
        <<" vertexMs="<<ms(phase.vertex_decode_ns,previous.vertex_decode_ns)
        <<" clipMs="<<ms(phase.triangle_prep_ns,previous.triangle_prep_ns)
        <<" batchMs="<<ms(phase.gpu_accumulate_ns,previous.gpu_accumulate_ns)
        <<" prims="<<(phase.primitives-previous.primitives)
        <<" vertices="<<(phase.vertices-previous.vertices);
    runtime_log_line(report.str());
    previous=phase;before_ge=ge;before_gpu=gpu;before_wait=wait;frames=0;started=now;
}
#endif

'''+marker)
    return one(s,'        report_realtime_speed_if_requested(rt, display_vblank_index);','        report_realtime_speed_if_requested(rt, display_vblank_index);\n#if defined(__ANDROID__)\n        android_pipeline_report();\n#endif')

def apply():
    if (ROOT/'android/REPAIR_069.md').exists():
        print('0.6.9 source migration already applied');return
    edit('lcs/host/lcs_audio_output_android.inc',audio)
    edit('lcs/host/lcs_android_audio_queue.hpp',queue)
    edit('lcs/host/ge_renderer.cpp',renderer)
    edit('lcs/host/lcs_ge_exec.cpp',executor)
    edit('lcs/host/ge_gpu_backend_gles.cpp',gles)
    edit('lcs/host/lcs_profile.cpp',pipeline)
    edit('lcs/host/lcs_android_gpu_policy.hpp',lambda s:s+'\nnamespace lcs {\n// Invalidate checks, not images pinned by pending primitives.\nvoid android_gles_texture_barrier() noexcept;\n}\n')
    edit('android/app/src/main/cpp/android_bridge.cpp',lambda s:one(s,'    setenv("LCS_GE_ASYNC", "0", 1);','    setenv("LCS_GE_ASYNC", "0", 1);\n    // Present the complete batch at guest vblank, not intermediate GE lists.\n    setenv("LCS_GE_NO_FRAME_SPLIT", "1", 1);\n    setenv("PSPRECOMP_GE_PHASE_DIAG", "1", 1);'))
    edit('android/app/build.gradle',lambda s:one(one(s,'versionCode 14','versionCode 15'),'versionName "0.6.8"','versionName "0.6.9"'))
    edit('android/app/src/main/java/com/ylports/lcsrecomp/MainActivity.java',lambda s:one(s,'v0.6.8 · GLES + audio fixes','v0.6.9 · Audio + frame stability'))
    changed['android/REPAIR_069.md']='''# Android 0.6.9

Baseline: d7c06ab7 (0.6.8).

- Preserve queued audio across guest timestamp jitter. Explicit channel reset still cancels that channel. Real underruns use a short cursor rebase, not another full silent lead.
- Log mixed samples, invalidated samples and starvation rebases separately from device xruns.
- Present complete ordered GE batches at guest vblank, rather than intermediate command-list changes. Existing guest timing remains enabled; GLES still has one owner thread.
- Revalidate texture contents on new lists, TEXFLUSH, TEXSYNC and CLUTLOAD without deleting versions pinned by queued draws.
- Prepare CPU lights once per primitive, reuse state revisions, trivially accept/reject triangles before polygon clipping, and skip disabled diagnostic bounds.
- Cache GLES pixel/transform uniforms and ES3 samplers. Keep draw order and framebuffer feedback handling.
- Log CPU GE, texture, vertex, clipping, submission, wait and remaining wall time separately.

## Verification
Tests exercise the actual producer/callback (AAudio driver mocked), CPU lighting/UV and clipping helpers, queues, real shaders and full GLES backend on Mesa. See CI artifacts for the executed results.

The audio regression supplies 46080 samples with guest-time jitter. 0.6.8 cancels almost all pending PCM; this revision must reproduce all supplied samples with zero jitter-induced resets. This does not synthesize missing audio when emulation is persistently slow.

No physical-device test is available yet for this revision. 60 FPS, continuous audio in every scene, and elimination of every visual defect are not certified. CPU submit timings are not GPU timer-query measurements.
'''
    for path,text in changed.items():
        p=ROOT/path;p.parent.mkdir(parents=True,exist_ok=True)
        temp=p.with_suffix(p.suffix+'.new');temp.write_text(text);temp.replace(p)
        print('Updated',path)
if __name__=='__main__':apply()
