// Actual audio implementation, with only the AAudio device API mocked.
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include "lcs_audio_output.cpp"
namespace lcs { void runtime_log_line(std::string_view) {} }
std::array<int16_t,882> samples(unsigned block) {
    std::array<int16_t,882> p{};
    for(unsigned i=0;i<441;i++) p[i*2]=p[i*2+1]=static_cast<int16_t>(1000+(block*441+i)%100);
    return p;
}
int main() {
    std::vector<int16_t> out; unsigned next=0;
    for(unsigned t=0;t<200;t++) {
        const bool stalled=(t>=20 && t<23)||(t>=90 && t<93);
        if(!stalled) while(next<=t) {
            auto p=samples(next);
            lcs::audio_output_submit(p,441,true,32768,32768,44100,0,next*10000,next*10000);++next;
        }
        auto &s=lcs::sound(); std::array<int16_t,882> b{};
        s.stream->data(s.stream,s.stream->user,b.data(),441);out.insert(out.end(),b.begin(),b.end());
    }
    for(unsigned i=0;i<4096;i++) assert(out[i*2]==0 && out[i*2+1]==0);
    for(unsigned i=4096;i<200*441;i++) assert(out[i*2]==1000+(i-4096)%100 && out[i*2+1]==out[i*2]);
    assert(lcs::sound().late.load()==0 && lcs::sound().dropped==0 && lcs::sound().resyncs==0);
    std::cout<<"PASS: two seconds of exact PCM, including two 30ms production stalls\n";
    auto &s=lcs::sound();std::atomic<bool> done{false};std::array<int16_t,512> b{};
    std::unique_lock lock(s.producer);
    std::thread consumer([&]{s.stream->data(s.stream,s.stream->user,b.data(),256);done.store(true);});
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!done.load() && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    const bool nonblocking=done.load();lock.unlock();consumer.join();assert(nonblocking);
    for(auto v:b)assert(v>=1000 && v<1100);
    std::cout<<"PASS: callback plays while producer mutex is held\n";
    lcs::audio_output_shutdown();
    std::array<int16_t,512> p;p.fill(20000);
    lcs::audio_output_submit(p,256,true,32768,32768,44100,0,0,0);
    lcs::audio_output_submit(p,256,true,32768,32768,44100,1,0,0);
    std::vector<int16_t> mix(4608*2);s.stream->data(s.stream,s.stream->user,mix.data(),4608);
    for(unsigned i=4096;i<4352;i++)assert(mix[2*i]==32767 && mix[2*i+1]==32767);
    lcs::audio_output_shutdown();
    constexpr unsigned chunks=30;unsigned produced=0;std::uint64_t audible=0;
    std::array<int16_t,3072> slow;slow.fill(1234);
    for(unsigned tick=0;tick<chunks*6+30;++tick) {
        if(tick%6==0 && produced<chunks) {
            lcs::audio_output_submit(slow,1536,true,32768,32768,44100,0,produced*60000u,produced*60000u);++produced;
        }
        std::array<int16_t,882> result{};
        s.stream->data(s.stream,s.stream->user,result.data(),441);
        for(unsigned i=0;i<441;i++)if(result[2*i]==1234)++audible;
    }
    std::cout<<"SLOW producer: supplied="<<chunks*1536<<" audible="<<audible<<" resyncs="<<s.resyncs<<'\n';
    if(audible!=chunks*1536 || s.resyncs!=0)return 20;
    lcs::audio_output_shutdown();
    std::cout<<"PASS: all 46080 supplied samples survive guest timestamp jitter\n";
}
