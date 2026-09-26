// Android 0.6.8: immutable timed packets; callback never locks the producer.
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
    std::uint64_t produced{}, rebases{};
    std::atomic<std::uint64_t> mixed{0}, empty_frames{0};
};
struct AndroidSound {
    std::mutex producer;
    AAudioStream *stream{};
    std::array<AndroidChannel,kAndroidChannels> channels;
    std::atomic<std::uint64_t> clock{0};
    std::atomic<std::uint64_t> submit_horizon{0};
    std::atomic<std::uint64_t> late{0};
    std::atomic<std::uint64_t> silent_frames{0};
    std::atomic<int> error{0};
    std::uint64_t guest_anchor{};
    std::uint64_t host_anchor{};
    std::uint64_t produced{};
    std::uint64_t dropped{};
    std::uint64_t resyncs{};
    std::uint64_t starvation_rebases{};
    std::atomic<std::uint64_t> mixed_samples{0};
    std::atomic<std::uint64_t> invalidated_samples{0};
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
    // Reserve the callback's whole output range before it starts mixing.
    // Producers may only schedule new packets beyond this horizon.
    s.submit_horizon.store(first+static_cast<std::uint64_t>(count),std::memory_order_release);
    for (int32_t base=0; base<count; base+=256) {
        const auto n=static_cast<std::size_t>(std::min<int32_t>(256,count-base));
        std::array<std::int32_t,512> mixed{};
        std::uint64_t late=0, contributed=0, invalidated=0;
        for (auto &c:s.channels) {
            const auto mixed_count=c.queue.mix(std::span<std::int32_t>(mixed.data(),n*2),
                first+static_cast<std::uint64_t>(base),
                c.generation.load(std::memory_order_acquire),late, &invalidated);
            c.mixed.fetch_add(mixed_count,std::memory_order_relaxed);
            c.empty_frames.fetch_add(n-mixed_count,std::memory_order_relaxed);
            contributed+=mixed_count;
        }
        s.late.fetch_add(late,std::memory_order_relaxed);
        s.mixed_samples.fetch_add(contributed,std::memory_order_relaxed);
        s.invalidated_samples.fetch_add(invalidated,std::memory_order_relaxed);
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
        c.produced=c.rebases=0; c.mixed.store(0); c.empty_frames.store(0);
        c.generation.fetch_add(1,std::memory_order_release);
    }
    s.clock.store(0); s.submit_horizon.store(0); s.late.store(0); s.silent_frames.store(0);
    s.anchored=false; s.produced=s.dropped=s.resyncs=s.starvation_rebases=0;
    s.mixed_samples.store(0); s.invalidated_samples.store(0);
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
    const auto read=s.submit_horizon.load(std::memory_order_acquire);
    const auto guest=sample_time(start_time_us);
    if (!s.anchored) {
        s.guest_anchor=sample_time(std::min(start_time_us,now_us));
        s.host_anchor=read+kAndroidLead; s.anchored=true;
    }
    auto &c=s.channels[channel];
    const auto gap=guest>c.expected_guest?guest-c.expected_guest:c.expected_guest-guest;
    // Small guest-time gaps are scheduling jitter, not channel replacement.
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
        if (c.cursor<read) {
            // A nearly empty but still continuous queue is NOT an underrun.
            // The old read+512 test inserted artificial gaps while samples
            // were still queued. Reserve one small callback quantum only after
            // a genuinely missed deadline, without discarding queued samples.
            c.cursor=read+128u;
            ++s.starvation_rebases; ++c.rebases;
        }
    }
    c.expected_guest=guest+(static_cast<std::uint64_t>(frames)*kAndroidRate+rate/2)/rate;
    TimedPcmQueue::Packet packet;
    packet.start=c.cursor; packet.generation=c.generation.load(std::memory_order_relaxed);
    const auto publish=[&]() {
        if (!packet.frames) return;
        if (c.queue.push(packet)) { s.produced+=packet.frames; c.produced+=packet.frames; }
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
    std::string per_channel;
    for (unsigned i=0;i<s.channels.size();++i) {
        const auto &c=s.channels[i];
        if (c.produced) per_channel += " ch"+std::to_string(i)+"="+std::to_string(c.produced)+
            "/"+std::to_string(c.mixed.load())+"/"+std::to_string(c.rebases);
    }
    runtime_log_line("audioChannels: produced/mixed/deadlineMisses"+per_channel);
    runtime_log_line("audio: produced="+std::to_string(s.produced)+
        " lateSamples="+std::to_string(s.late.load())+
        " dropped="+std::to_string(s.dropped)+" resyncs="+std::to_string(s.resyncs)+
        " starvationRebases="+std::to_string(s.starvation_rebases)+
        " mixedSamples="+std::to_string(s.mixed_samples.load())+
        " invalidatedSamples="+std::to_string(s.invalidated_samples.load())+
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
