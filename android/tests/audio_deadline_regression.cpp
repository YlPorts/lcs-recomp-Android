// Only the device is mocked: real timed queues, resampler and callback.
#include <cassert>
#include <iostream>
#include <vector>
#include "lcs_audio_output.cpp"
namespace lcs { void runtime_log_line(std::string_view) {} }
int main() {
    std::vector<std::int16_t> effect(512*2,1000), music(4096*2,2000);
    lcs::audio_output_submit(effect,512,true,32768,0,44100,0,0,0);
    lcs::audio_output_submit(music,4096,true,0,32768,44100,1,0,0);
    auto &s=lcs::sound();
    std::vector<std::int16_t> initial(4352*2);
    s.stream->data(s.stream,s.stream->user,initial.data(),4352);
    // Effects still have 256 queued samples; the music channel has ample lead.
    lcs::audio_output_submit(effect,512,true,32768,0,44100,0,11610,11610);
    std::vector<std::int16_t> out(768*2);
    s.stream->data(s.stream,s.stream->user,out.data(),768);
    for(unsigned i=0;i<768;i++) {
        if(out[i*2]!=1000 || out[i*2+1]!=2000) {
            std::cerr<<"FAIL: artificial effects gap while PCM remained queued at sample "<<i
                     <<" left="<<out[i*2]<<" right="<<out[i*2+1]<<'\n';return 1;
        }
    }
    assert(s.starvation_rebases==0 && s.dropped==0 && s.resyncs==0);
    // An actual missed deadline is distinguished from a low but valid lead.
    std::vector<std::int16_t> drain(1024*2);s.stream->data(s.stream,s.stream->user,drain.data(),1024);
    lcs::audio_output_submit(effect,512,true,32768,0,44100,0,23220,23220);
    assert(s.starvation_rebases==1);
    std::vector<std::int16_t> recovered(640*2);s.stream->data(s.stream,s.stream->user,recovered.data(),640);
    for(unsigned i=128;i<640;i++)assert(recovered[2*i]==1000);
    assert(s.dropped==0 && s.resyncs==0);
    std::cout<<"PASS: 768 uninterrupted effects samples with a nearly empty queue; music channel unchanged; actual missed deadline recovers all 512 samples\n";
    lcs::audio_output_shutdown();
}
