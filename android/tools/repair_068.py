#!/usr/bin/env python3
"""Apply reviewed Android 0.6.8 fixes once to the known 0.6.7 source tree.
No network or Git operations. Validate every source before writing any files.
"""
from pathlib import Path
import hashlib
import re
import sys
ROOT = Path(__file__).resolve().parents[2] if len(sys.argv) == 1 else Path(sys.argv[1]).resolve()
MARKER = ROOT / 'android/REPAIR_068.md'
def once(text, old, new):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'Expected one patch anchor, got {count}: {old[:110]!r}')
    return text.replace(old, new, 1)
def function(text, name, replacement):
    match = re.search(r'(?m)^[\w:<>, &*]+\b' + re.escape(name) + r'\([^;]*?\)\s*(?:noexcept\s*)?\{', text)
    if match is None:
        raise RuntimeError(f'Function not found: {name}')
    start, pos, depth = match.start(), match.end(), 1
    while depth:
        if pos >= len(text): raise RuntimeError(f'Unbalanced body: {name}')
        if text.startswith('//', pos):
            pos = text.find('\n', pos)
            if pos < 0: raise RuntimeError('Unterminated comment')
        elif text.startswith('/*', pos): pos = text.index('*/', pos + 2) + 2
        elif text[pos] in '\"\'':
            quote = text[pos]; pos += 1
            while text[pos] != quote: pos += 2 if text[pos] == '\\' else 1
            pos += 1
        else:
            depth += (text[pos] == '{') - (text[pos] == '}'); pos += 1
    return text[:start] + replacement.rstrip() + text[pos:]
def read(path, blob_sha=''):
    data = (ROOT / path).read_bytes()
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if blob_sha and actual != blob_sha:
        raise RuntimeError(f'{path}: baseline mismatch {actual}, expected {blob_sha}')
    return data.decode('utf-8')
POLICY = r'''#pragma once
#include <cstdint>
#include <unordered_map>
namespace lcs::android_detail {
// Preserve RAM versus VRAM before normalizing uncached/mirrored addresses.
inline bool is_vram_address(std::uint32_t address) noexcept {
    const std::uint32_t physical = address & 0x3FFFFFFFu;
    return physical >= 0x04000000u && physical < 0x04800000u;
}
inline std::uint32_t vram_offset(std::uint32_t address) noexcept {
    return address & 0x001FFFF0u;
}
class TextureVersions {
    struct Entry { std::uint64_t signature{}; std::uint64_t epoch{}; };
    std::unordered_map<std::uint64_t, Entry> entries_;
public:
    void clear() noexcept { entries_.clear(); }
    void observe(std::uint64_t base, std::uint64_t signature, std::uint64_t epoch) {
        if (signature != 0u) entries_[base] = Entry{signature, epoch};
    }
    std::uint64_t resolve(std::uint64_t base, std::uint64_t explicit_signature) const noexcept {
        if (explicit_signature != 0u) return explicit_signature;
        const auto it = entries_.find(base);
        return it == entries_.end() ? 0u : it->second.signature;
    }
    bool needs_check(std::uint64_t base, std::uint64_t epoch) const noexcept {
        const auto it = entries_.find(base);
        return it == entries_.end() || it->second.epoch != epoch;
    }
};
inline bool color_test_pass(unsigned function, std::uint32_t rgb,
                            std::uint32_t reference, std::uint32_t mask) noexcept {
    const bool equal = (rgb & mask & 0xFFFFFFu) == (reference & mask & 0xFFFFFFu);
    switch (function & 3u) {
    case 0u: return false;
    case 1u: return true;
    case 2u: return equal;
    default: return !equal;
    }
}
} // namespace lcs::android_detail
'''
AUDIO_QUEUE = r'''#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
namespace lcs::android_detail {
// One serialized HLE producer and one AAudio consumer. Published packets are
// immutable. No locks, allocations or waits in mix().
class TimedPcmQueue {
public:
    static constexpr std::size_t kFrames = 256;
    static constexpr std::size_t kPackets = 256;
    struct Packet {
        std::uint64_t start{};
        std::uint32_t generation{};
        std::uint32_t frames{};
        std::array<std::int32_t, kFrames * 2> samples{};
    };
private:
    std::array<Packet, kPackets> packets_{};
    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
    std::size_t offset_{};
public:
    bool push(const Packet &packet) noexcept {
        if (packet.frames == 0 || packet.frames > kFrames) return false;
        const auto w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= kPackets) return false;
        packets_[w % kPackets] = packet;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    std::uint64_t mix(std::span<std::int32_t> out, std::uint64_t first,
                      std::uint32_t generation, std::uint64_t &late) noexcept {
        const auto frames = out.size() / 2;
        std::uint64_t contributed = 0;
        auto r = read_.load(std::memory_order_relaxed);
        std::size_t dst = 0;
        while (dst < frames) {
            if (r == write_.load(std::memory_order_acquire)) break;
            const Packet &p = packets_[r % kPackets];
            if (p.generation != generation || offset_ >= p.frames) {
                offset_ = 0;
                read_.store(++r, std::memory_order_release);
                continue;
            }
            std::uint64_t cursor = p.start + offset_;
            if (cursor < first + dst) {
                const auto skip = static_cast<std::size_t>(std::min<std::uint64_t>(
                    first + dst - cursor, p.frames - offset_));
                offset_ += skip; late += skip; continue;
            }
            if (cursor > first + dst) {
                const auto gap = static_cast<std::size_t>(std::min<std::uint64_t>(
                    cursor - (first + dst), frames - dst));
                dst += gap; continue;
            }
            const auto n = std::min(frames - dst, p.frames - offset_);
            for (std::size_t i = 0; i < n * 2; ++i)
                out[dst * 2 + i] += p.samples[offset_ * 2 + i];
            offset_ += n; dst += n; contributed += n;
        }
        return contributed;
    }
    void reset_stopped() noexcept {
        // Close the stream and stop its consumer before resetting.
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
        offset_ = 0;
    }
};
} // namespace lcs::android_detail
'''
TESTS = r'''#include "lcs_android_gpu_policy.hpp"
#include "lcs_android_audio_queue.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <thread>
using namespace lcs::android_detail;
int main() {
    assert(is_vram_address(0x04088000));
    assert(is_vram_address(0x44088000));
    assert(!is_vram_address(0x08888000));
    assert(!is_vram_address(0x48888000));
    assert(vram_offset(0x04088000) == vram_offset(0x44088000));
    TextureVersions v;
    v.observe(42, 101, 1);
    assert(v.resolve(42, 0) == 101);
    assert(!v.needs_check(42, 1));
    assert(v.needs_check(42, 2));
    auto queued = v.resolve(42, 0);
    v.observe(42, 202, 2);
    assert(v.resolve(42, 0) == 202);
    assert(v.resolve(42, queued) == 101);
    assert(color_test_pass(3, 0xff0000, 0x00ff00, 0xffffff));
    assert(!color_test_pass(3, 0x00ff00, 0x00ff00, 0xffffff));
    assert(color_test_pass(2, 0xaaffbb, 0x00ff00, 0x00ff00));
    TimedPcmQueue q;
    TimedPcmQueue::Packet p;
    p.start=10; p.generation=1; p.frames=4;
    for (unsigned i=0;i<8;i++) p.samples[i]=100+i;
    assert(q.push(p));
    std::array<std::int32_t,32> mixed{};
    std::uint64_t late=0;
    assert(q.mix(mixed,0,1,late)==4);
    assert(mixed[20]==100 && mixed[27]==107 && mixed[28]==0);
    mixed.fill(0);
    assert(q.mix(mixed,16,1,late)==0);
    p.start=0; assert(q.push(p));
    assert(q.mix(mixed,32,1,late)==0 && late==4);
    p.start=48; p.generation=1; assert(q.push(p));
    assert(q.mix(mixed,48,2,late)==0);
    q.reset_stopped();
    p.start=0; p.generation=2;
    for (std::size_t i=0;i<TimedPcmQueue::kPackets;i++) assert(q.push(p));
    assert(!q.push(p));
    q.reset_stopped();
    constexpr std::uint64_t packets=10000;
    std::thread producer([&] {
        for (std::uint64_t n=0;n<packets;n++) {
            TimedPcmQueue::Packet a; a.start=n*256; a.frames=256; a.generation=3;
            a.samples.fill(static_cast<std::int32_t>(n+1));
            while (!q.push(a)) std::this_thread::yield();
        }
    });
    for (std::uint64_t n=0;n<packets;n++) {
        std::array<std::int32_t,512> block{};
        while (q.mix(block,n*256,3,late)==0) std::this_thread::yield();
        for (auto x:block) assert(x==static_cast<std::int32_t>(n+1));
    }
    producer.join();
    std::cout << "PASS: VRAM aliases, version pinning, color test, timed PCM, reset, overflow, 10000 concurrent packets\n";
}
'''
AUDIO_IMPL = r'''// Android 0.6.8: immutable timed packets; callback never locks the producer.
#include <aaudio/AAudio.h>
#include "lcs_android_audio_queue.hpp"
#include "lcs_runtime_log.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
namespace lcs {
namespace {
constexpr std::uint32_t kAndroidRate = StreamingLinearResampler::kOutputRate;
constexpr std::size_t kAndroidChannels = 9;
constexpr std::uint64_t kAndroidLead = 4096;
using android_detail::TimedPcmQueue;
struct AndroidChannel {
    TimedPcmQueue queue;
    StreamingLinearResampler resampler;
    std::atomic<std::uint32_t> generation{1};
    std::uint64_t cursor{};
    std::uint64_t expected_guest{};
    std::uint32_t rate{};
    bool stereo{};
    bool active{};
};
struct AndroidSound {
    std::mutex producer;
    AAudioStream *stream{};
    std::array<AndroidChannel,kAndroidChannels> channels;
    std::atomic<std::uint64_t> clock{0};
    std::atomic<std::uint64_t> late{0};
    std::atomic<std::uint64_t> silent_frames{0};
    std::atomic<int> error{0};
    std::uint64_t guest_anchor{};
    std::uint64_t host_anchor{};
    std::uint64_t produced{};
    std::uint64_t dropped{};
    std::uint64_t resyncs{};
    bool anchored{};
    std::chrono::steady_clock::time_point last_log{};
};
AndroidSound &sound() { static AndroidSound s; return s; }
std::uint64_t sample_time(std::uint64_t us) noexcept {
    return (us / 1000000u) * kAndroidRate +
           ((us % 1000000u) * kAndroidRate + 500000u) / 1000000u;
}
aaudio_data_callback_result_t android_audio_callback(
    AAudioStream *, void *opaque, void *buffer, int32_t count) {
    auto &s=*static_cast<AndroidSound*>(opaque);
    auto *out=static_cast<std::int16_t*>(buffer);
    if (!out || count<=0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    auto first=s.clock.load(std::memory_order_relaxed);
    for (int32_t base=0; base<count; base+=256) {
        const auto n=static_cast<std::size_t>(std::min<int32_t>(256,count-base));
        std::array<std::int32_t,512> mixed{};
        std::uint64_t late=0, contributed=0;
        for (auto &c:s.channels)
            contributed += c.queue.mix(std::span<std::int32_t>(mixed.data(),n*2),
                first+static_cast<std::uint64_t>(base),
                c.generation.load(std::memory_order_acquire),late);
        s.late.fetch_add(late,std::memory_order_relaxed);
        if (!contributed) s.silent_frames.fetch_add(n,std::memory_order_relaxed);
        for (std::size_t i=0;i<n*2;i++)
            out[static_cast<std::size_t>(base)*2+i]=
                static_cast<std::int16_t>(std::clamp(mixed[i],-32768,32767));
    }
    s.clock.store(first+static_cast<std::uint64_t>(count),std::memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void android_audio_error(AAudioStream *,void *opaque,aaudio_result_t error) {
    static_cast<AndroidSound*>(opaque)->error.store(error,std::memory_order_release);
}
void reset_audio_stopped(AndroidSound &s) {
    for (auto &c:s.channels) {
        c.queue.reset_stopped(); c.active=false;
        c.generation.fetch_add(1,std::memory_order_release);
    }
    s.clock.store(0); s.late.store(0); s.silent_frames.store(0);
    s.anchored=false; s.produced=s.dropped=s.resyncs=0;
}
bool open_android_audio(AndroidSound &s) {
    if (s.stream && s.error.load(std::memory_order_acquire)==0) return true;
    if (s.stream) {
        AAudioStream_requestStop(s.stream); AAudioStream_close(s.stream); s.stream=nullptr;
    }
    reset_audio_stopped(s); s.error.store(0);
    AAudioStreamBuilder *b=nullptr;
    if (AAudio_createStreamBuilder(&b)!=AAUDIO_OK || !b) return false;
    AAudioStreamBuilder_setDirection(b,AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b,AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b,2);
    AAudioStreamBuilder_setSampleRate(b,kAndroidRate);
    AAudioStreamBuilder_setSharingMode(b,AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setBufferCapacityInFrames(b,4096);
    AAudioStreamBuilder_setDataCallback(b,android_audio_callback,&s);
    AAudioStreamBuilder_setErrorCallback(b,android_audio_error,&s);
    const auto result=AAudioStreamBuilder_openStream(b,&s.stream);
    AAudioStreamBuilder_delete(b);
    if (result!=AAUDIO_OK || !s.stream) {
        s.stream=nullptr;
        runtime_log_line("audio: open failed="+std::to_string(result)); return false;
    }
    const auto burst=AAudioStream_getFramesPerBurst(s.stream);
    (void)AAudioStream_setBufferSizeInFrames(s.stream,std::max(512,burst*3));
    if (AAudioStream_requestStart(s.stream)!=AAUDIO_OK) {
        AAudioStream_close(s.stream); s.stream=nullptr; return false;
    }
    runtime_log_line("audio: timed PCM callback, rate="+std::to_string(kAndroidRate)+
                     " burst="+std::to_string(burst)+" lead="+std::to_string(kAndroidLead));
    return true;
}
} // namespace
bool audio_output_enabled() {
    const char *value=std::getenv("PSPRECOMP_AUDIO");
    return !value || std::strcmp(value,"0")!=0;
}
void audio_output_submit(std::span<const std::int16_t> pcm,std::uint32_t frames,
    bool stereo,std::uint32_t left,std::uint32_t right,std::uint32_t rate,
    std::uint32_t channel,std::uint64_t start_time_us,std::uint64_t now_us) {
    if (!audio_output_enabled() || !frames || channel>=kAndroidChannels ||
        pcm.size()<static_cast<std::size_t>(frames)*(stereo?2u:1u)) return;
    if (!rate) rate=kAndroidRate;
    auto &s=sound();
    // This mutex serializes HLE producers only; the callback never acquires it.
    std::lock_guard<std::mutex> lock(s.producer);
    if (!open_android_audio(s)) return;
    const auto read=s.clock.load(std::memory_order_acquire);
    const auto guest=sample_time(start_time_us);
    if (!s.anchored) {
        s.guest_anchor=sample_time(std::min(start_time_us,now_us));
        s.host_anchor=read+kAndroidLead; s.anchored=true;
    }
    auto &c=s.channels[channel];
    const auto gap=guest>c.expected_guest?guest-c.expected_guest:c.expected_guest-guest;
    const bool changed=c.active && (c.rate!=rate || c.stereo!=stereo || gap>128);
    const bool late=c.active && c.cursor<read+128;
    if (!c.active || changed || late) {
        if (c.active) ++s.resyncs;
        c.generation.fetch_add(1,std::memory_order_release);
        const auto scheduled=guest>=s.guest_anchor
            ? s.host_anchor+guest-s.guest_anchor : read+kAndroidLead;
        c.cursor=std::max(scheduled,read+kAndroidLead);
        if (c.cursor>read+kAndroidRate) c.cursor=read+kAndroidLead;
        c.rate=rate; c.stereo=stereo; c.active=true;
        c.resampler.reset(rate,stereo);
    }
    c.expected_guest=guest+(static_cast<std::uint64_t>(frames)*kAndroidRate+rate/2)/rate;
    TimedPcmQueue::Packet packet;
    packet.start=c.cursor; packet.generation=c.generation.load(std::memory_order_relaxed);
    const auto publish=[&]() {
        if (!packet.frames) return;
        if (c.queue.push(packet)) s.produced+=packet.frames;
        else s.dropped+=packet.frames;
        packet.frames=0; packet.start=c.cursor;
    };
    c.resampler.process(pcm,frames,stereo,rate,[&](std::int16_t l,std::int16_t r) {
        constexpr std::int64_t limit=0x0FFFFFFF;
        const auto i=static_cast<std::size_t>(packet.frames)*2;
        packet.samples[i]=static_cast<std::int32_t>(std::clamp<std::int64_t>(
            (static_cast<std::int64_t>(l)*left)>>15,-limit,limit));
        packet.samples[i+1]=static_cast<std::int32_t>(std::clamp<std::int64_t>(
            (static_cast<std::int64_t>(r)*right)>>15,-limit,limit));
        ++packet.frames; ++c.cursor;
        if (packet.frames==TimedPcmQueue::kFrames) publish();
    });
    publish();
}
void audio_output_advance(std::uint64_t) {
    auto &s=sound(); std::lock_guard<std::mutex> lock(s.producer);
    const auto now=std::chrono::steady_clock::now();
    if (!s.stream || now-s.last_log<std::chrono::seconds(2)) return;
    s.last_log=now;
    runtime_log_line("audio: produced="+std::to_string(s.produced)+
        " lateSamples="+std::to_string(s.late.load())+
        " dropped="+std::to_string(s.dropped)+" resyncs="+std::to_string(s.resyncs)+
        " silentCallbackFrames="+std::to_string(s.silent_frames.load())+
        " xruns="+std::to_string(AAudioStream_getXRunCount(s.stream)));
}
void audio_output_reset_channel(std::uint32_t channel) {
    if (channel>=kAndroidChannels) return;
    auto &s=sound(); std::lock_guard<std::mutex> lock(s.producer);
    auto &c=s.channels[channel]; c.generation.fetch_add(1,std::memory_order_release); c.active=false;
}
void audio_output_shutdown() {
    auto &s=sound(); std::lock_guard<std::mutex> lock(s.producer);
    if (s.stream) {
        AAudioStream_requestStop(s.stream); AAudioStream_close(s.stream); s.stream=nullptr;
    }
    reset_audio_stopped(s); s.error.store(0);
}
} // namespace lcs
'''
def patch_sources():
    files = {
        'lcs/host/lcs_android_gpu_policy.hpp': POLICY,
        'lcs/host/lcs_android_audio_queue.hpp': AUDIO_QUEUE,
        'lcs/host/lcs_audio_output_android.inc': AUDIO_IMPL,
        'android/tests/android_regression.cpp': TESTS,
    }
    path = 'lcs/host/ge_gpu_backend_gles.cpp'
    c = read(path, '55aff526306145dc68cd8a373e9393e9089d293f')
    c = once(c, '#include "android_host.hpp"', '#include "android_host.hpp"\n#include "lcs_android_gpu_policy.hpp"')
    c = once(c, '    std::unordered_map<std::uint64_t, std::uint64_t> texture_signature_epochs;', '    android_detail::TextureVersions texture_versions;')
    c = once(c, '    s.texture_signature_epochs.clear();', '    s.texture_versions.clear();')
    c = once(c, '        s.texture_signature_epochs[texture_key(draw)] = s.frame_epoch;', '        s.texture_versions.observe(texture_key(draw), draw.texture_content_signature, s.frame_epoch);')
    c = function(c, 'texture_lookup_key', r'''std::uint64_t texture_lookup_key(const GeGpuDrawDescriptor &draw) noexcept {
    const auto base = texture_key(draw);
    const auto signature = state().texture_versions.resolve(base, draw.texture_content_signature);
    return signature ? hash_mix(base, signature) : base;
}
GeGpuDrawDescriptor snapshot_texture_draw(const GeGpuDrawDescriptor &draw) {
    auto snapshot = draw;
    if (snapshot.texture_enabled)
        snapshot.texture_content_signature = state().texture_versions.resolve(
            texture_key(draw), draw.texture_content_signature);
    return snapshot;
}
auto feedback_target(GlesState &s, std::uint32_t address) {
    if (!android_detail::is_vram_address(address)) return s.targets.end();
    return s.targets.find(android_detail::vram_offset(address));
}
auto feedback_target(const GlesState &s, std::uint32_t address) {
    if (!android_detail::is_vram_address(address)) return s.targets.end();
    return s.targets.find(android_detail::vram_offset(address));
}''')
    c = function(c, 'ge_gpu_backend_texture_signature_needed', r'''bool ge_gpu_backend_texture_signature_needed(const GeGpuDrawDescriptor &draw) noexcept {
    auto &s = state();
    if (!s.enabled || !draw.texture_enabled || !draw.texture_width || !draw.texture_height)
        return false;
    if (feedback_target(s, draw.texture_address) != s.targets.end()) return false;
    return s.texture_versions.needs_check(texture_key(draw), s.frame_epoch);
}''')
    c = c.replace('s.targets.find(draw.texture_address & 0x001FFFF0u)', 'feedback_target(s, draw.texture_address)')
    c = once(c, 'const auto framebuffer = s.targets.find(address);', 'const auto framebuffer = feedback_target(s, draw.texture_address);')
    c = once(c, 'if (s.targets.find(feedback_address) != s.targets.end()) {', 'if (feedback_target(s, draw.texture_address) != s.targets.end()) {')
    for name in ['append_or_merge_color_batch', 'ge_gpu_backend_accumulate_hardware_triangles']:
        begin = c.index(name+'('); end = c.index('\n}', begin)+2
        chunk = c[begin:end]
        chunk = once(chunk, 'const GeGpuDrawDescriptor &draw,', 'const GeGpuDrawDescriptor &input_draw,')
        body = chunk.index('{')+1
        chunk = chunk[:body] + '\n    const GeGpuDrawDescriptor draw = snapshot_texture_draw(input_draw);' + chunk[body:]
        c = c[:begin]+chunk+c[end:]
    c = once(c, '    if (a.texture_enabled != b.texture_enabled) return false;', r'''    // Feedback reads are order-dependent and must not be merged.
    if ((a.texture_enabled && feedback_target(state(), a.texture_address) != state().targets.end()) ||
        (b.texture_enabled && feedback_target(state(), b.texture_address) != state().targets.end())) return false;
    if (a.color_test_enabled != b.color_test_enabled ||
        a.color_test_function != b.color_test_function ||
        a.color_test_reference != b.color_test_reference ||
        a.color_test_mask != b.color_test_mask) return false;
    if (a.texture_enabled != b.texture_enabled) return false;''')
    c = once(c, '    if (draw.depth_test_enabled) {\n        glEnable(GL_DEPTH_TEST);\n        glDepthFunc(depth_func(draw.depth_function));', '    if (draw.depth_test_enabled || (draw.clear_mode && draw.clear_depth)) {\n        glEnable(GL_DEPTH_TEST);\n        glDepthFunc(draw.clear_mode ? GL_ALWAYS : depth_func(draw.depth_function));')
    c = once(c, '    const std::thread::id current_thread = std::this_thread::get_id();', r'''    const std::thread::id current_thread = std::this_thread::get_id();
    if (s.gl_ready && s.gl_thread == current_thread && s.surface != EGL_NO_SURFACE &&
        !s.surface_dirty.load(std::memory_order_acquire)) return true;''')
    c = once(c, '    GLint u_tex{-1};', r'''    GLint u_tex{-1};
    GLint u_texture_flip_v{-1};
    GLint u_color_test{-1};
    GLint u_color_reference{-1};
    GLint u_color_mask{-1};''')
    c = once(c, 'uniform sampler2D uTexture;', r'''uniform sampler2D uTexture;
uniform int uTextureFlipV;
uniform int uColorTest;
uniform ivec3 uColorReference;
uniform ivec3 uColorMask;''')
    c = once(c, '        vec2 sampleUv = vUv / q;', '        vec2 sampleUv = vUv / q;\n        if (uTextureFlipV != 0) sampleUv.y = 1.0 - sampleUv.y;')
    c = once(c, '    if (uFogEnabled != 0)\n        color.rgb', r'''    if (uColorTest >= 0) {
        ivec3 rgb = ivec3(floor(clamp(color.rgb, 0.0, 1.0)*255.0+0.5)) & uColorMask;
        ivec3 reference = uColorReference & uColorMask;
        bool rgbEqual = all(equal(rgb, reference));
        if (uColorTest == 0 || (uColorTest == 2 && !rgbEqual) ||
            (uColorTest == 3 && rgbEqual)) discard;
    }
    if (uFogEnabled != 0)
        color.rgb''')
    c = once(c, '    s.u_tex = glGetUniformLocation(s.program, "uTexture");', r'''    s.u_tex = glGetUniformLocation(s.program, "uTexture");
    s.u_texture_flip_v = glGetUniformLocation(s.program, "uTextureFlipV");
    s.u_color_test = glGetUniformLocation(s.program, "uColorTest");
    s.u_color_reference = glGetUniformLocation(s.program, "uColorReference");
    s.u_color_mask = glGetUniformLocation(s.program, "uColorMask");''')
    c = once(c, '    glUniform1i(s.u_tex, 0);', r'''    glUniform1i(s.u_tex, 0);
    glUniform1i(s.u_texture_flip_v,
        draw.texture_enabled && feedback_target(s, draw.texture_address) != s.targets.end());
    glUniform1i(s.u_color_test, draw.color_test_enabled && !draw.clear_mode
        ? static_cast<GLint>(draw.color_test_function & 3u) : -1);
    const auto rgb_uniform = [](GLint location, std::uint32_t packed) {
        glUniform3i(location, packed & 255u, (packed >> 8u) & 255u, (packed >> 16u) & 255u);
    };
    rgb_uniform(s.u_color_reference, draw.color_test_reference);
    rgb_uniform(s.u_color_mask, draw.color_test_mask);''')
    c = once(c, '    std::uint64_t perf_frame_count{};', r'''    std::uint64_t perf_frame_count{};
    std::chrono::steady_clock::time_point perf_window{};
    std::uint64_t perf_presents{};
    std::uint64_t perf_epochs{};
    std::uint64_t perf_color_tests{};''')
    c = once(c, '    if (textured) ++s.report.textured_game_draw_calls;', '    if (batch.draw.color_test_enabled) ++s.perf_color_tests;\n    if (textured) ++s.report.textured_game_draw_calls;')
    c = once(c, '    s.report.game_frame_vblank = vblank;', r'''    const auto perf_now = std::chrono::steady_clock::now();
    if (s.perf_window == std::chrono::steady_clock::time_point{}) s.perf_window = perf_now;
    ++s.perf_epochs;
    if (presented) ++s.perf_presents;
    const double wall_s = std::chrono::duration<double>(perf_now - s.perf_window).count();
    if (wall_s >= 2.0) {
        runtime_log_line("perf: presentFPS=" + std::to_string(s.perf_presents/wall_s) +
            " presentCalls=" + std::to_string(s.perf_presents) +
            " finishCalls=" + std::to_string(s.perf_epochs) +
            " wallSec=" + std::to_string(wall_s) +
            " colorTestDraws=" + std::to_string(s.perf_color_tests));
        s.perf_presents = s.perf_epochs = s.perf_color_tests = 0;
        s.perf_window = perf_now;
    }
    s.report.game_frame_vblank = vblank;''')
    c = c.replace('" avgGlesMs="', '" cpuSubmitMs="')
    files[path] = c
    path = 'lcs/host/ge_gpu_backend.hpp'
    c = read(path); start = c.index('struct GeGpuDrawDescriptor'); end = c.index('\n};', start)
    chunk = c[start:end] + '\n    bool color_test_enabled{};\n    std::uint32_t color_test_function{};\n    std::uint32_t color_test_reference{};\n    std::uint32_t color_test_mask{0xFFFFFFu};\n'
    files[path] = c[:start]+chunk+c[end:]
    path = 'lcs/host/ge_renderer.cpp'
    c = read(path, 'ef47d37743b88f1e33e9f37776d20ae0be827c52')
    c = once(c, '        gpu_draw.primitive = primitive;', r'''        // Read these independently of cached descriptor revisions.
        gpu_draw.color_test_enabled = (data24(commands[0x27u]) & 1u) != 0u;
        gpu_draw.color_test_function = data24(commands[0xD8u]) & 3u;
        gpu_draw.color_test_reference = data24(commands[0xD9u]) & 0xFFFFFFu;
        gpu_draw.color_test_mask = data24(commands[0xDAu]) & 0xFFFFFFu;
        gpu_draw.primitive = primitive;''')
    c = once(c, '    draw.texture_enabled = false;\n    draw.blend_enabled = false;', '    draw.texture_enabled = false;\n    draw.color_test_enabled = false;\n    draw.blend_enabled = false;')
    c = once(c, '        for (std::uint32_t i = 0u; i < count; ++i) {\n            const std::uint32_t index = draw_indices(i);', r'''        static thread_local std::array<Vertex, 256> reuse_vertices;
        std::array<std::uint32_t, 256> reuse_indices{};
        std::array<bool, 256> reuse_valid{};
        const bool reuse_indexed = gpu_backend_enabled && layout.index_type != 0u;
        for (std::uint32_t i = 0u; i < count; ++i) {
            const std::uint32_t index = draw_indices(i);
            const auto slot = index & 255u;
            if (reuse_indexed && reuse_valid[slot] && reuse_indices[slot] == index) {
                const Vertex &vertex = reuse_vertices[slot];
                record_clip_vertex(stats, vertex);
                if (layout.through) record_screen_vertex(stats, vertex);
                vertices.push_back(vertex);
                continue;
            }''')
    anchor = '            record_clip_vertex(stats, vertex);\n            if (layout.through) record_screen_vertex(stats, vertex);\n            vertices.push_back(vertex);'
    c = once(c, anchor, r'''            if (reuse_indexed) {
                reuse_vertices[slot] = vertex;
                reuse_indices[slot] = index;
                reuse_valid[slot] = true;
            }
''' + anchor)
    files[path] = c
    path = 'lcs/host/lcs_audio_output.cpp'
    c = read(path, '5938cde7d77dea07f613d2f386ca94df5a6945db')
    begin = c.index('#elif defined(__ANDROID__)'); end = c.index('\n#else\n', begin)
    files[path] = c[:begin]+'#elif defined(__ANDROID__)\n#include "lcs_audio_output_android.inc"\n'+c[end:]
    path = 'android/app/src/main/cpp/CMakeLists.txt'
    c = read(path, 'cefea3fbe2c1b8acc8866e83a700704ffd01982f')
    c = once(c, 'target_compile_options(lcs_generated PRIVATE -O1 -g0 -fexceptions -frtti)', 'target_compile_options(lcs_generated PRIVATE -O2 -g0 -fexceptions -frtti)')
    c = c.replace('# Iteration APK: keep the huge generated AOT corpus at O1 so ccache can\n# reuse the validated mobile objects. The software GE itself is still O3+NEON,\n# which is the dominant performance target in this build.', '# Optimized guest AOT code. Preserve IEEE semantics: no guest fast-math.')
    files[path] = c
    path = 'android/app/build.gradle'
    c = read(path)
    if 'versionName "0.6.7"' not in c: raise RuntimeError('Version is not 0.6.7')
    c = re.sub(r'versionCode\s+(\d+)', lambda m: f'versionCode {int(m[1])+1}', c, count=1)
    c = once(c, 'versionName "0.6.7"', 'versionName "0.6.8"'); files[path] = c
    path = 'android/app/src/main/java/com/ylports/lcsrecomp/MainActivity.java'
    c = read(path)
    c = re.sub(r'info\.setText\("v0\.6\.7[^"\n]*"\);', lambda _: 'info.setText("v0.6.8 · GLES + audio fixes\\nGTA: Liberty City Stories · ULUS-10041 v1.05");', c)
    files[path] = c
    files['android/REPAIR_068.md'] = '''# Android 0.6.8\n\n- Resolve skipped texture signatures to the last validated content and pin them when queuing a draw.\n- Restrict framebuffer feedback lookup to PSP VRAM, including uncached aliases.\n- Preserve ordering for feedback primitives and flip only GLES-rendered feedback.\n- Clear-depth draws write depth with GL_ALWAYS even when PSP Z test is disabled.\n- Implement PSP colour tests in GLES; do not remove green with a colour heuristic.\n- Reuse indexed-vertex transforms within each primitive; AOT uses O2 without fast-math.\n- Replace the mutex-dependent AAudio consumer with immutable timed PCM packets.\n- Log actual successful presentations per wall-clock second separately from CPU submit cost.\n\nThis does not certify 60 FPS or game-wide visual accuracy on a physical device.\nSynthetic tests verify components; a complete game run remains necessary.\n'''
    return files
def apply():
    if MARKER.exists():
        print('0.6.8 migration already applied; no changes'); return
    files = patch_sources()
    for name, content in files.items():
        path = ROOT / name; path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_suffix(path.suffix+'.new')
        temp.write_text(content, encoding='utf-8'); temp.replace(path)
        print('Updated', name)
if __name__=='__main__': apply()
