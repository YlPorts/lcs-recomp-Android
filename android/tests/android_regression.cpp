#include "lcs_android_gpu_policy.hpp"
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
