#include "lcs_media_decoder.hpp"
#include "lcs_runtime_log.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
extern "C" {
#include <libavcodec/avcodec.h>
}
namespace lcs { void runtime_log_line(std::string_view s) {std::cout<<s<<'\n';} }
int main(int argc,char **argv){
    assert(avcodec_find_decoder(AV_CODEC_ID_ATRAC3));
    assert(avcodec_find_decoder(AV_CODEC_ID_ATRAC3P));
    // Use FFmpeg's public codec samples solely as CI input, not APK assets.
    assert(argc==3);
    for(int i=1;i<argc;i++) {
        lcs::AudioStreamDecoder decoder;
        assert(decoder.open(argv[i],44100,2,0));
        std::array<std::uint8_t,4096> pcm{};
        std::size_t got=0;std::uint64_t nonzero=0;
        for(int j=0;j<100;j++){
            auto n=decoder.read(pcm);got+=n;
            for(std::size_t k=0;k<n;k++)nonzero+=pcm[k]!=0;
            if(n<pcm.size())break;
        }
        assert(got>=8192 && nonzero>100);
        decoder.close();assert(!decoder.is_open());
        assert(!decoder.open("/this-file-does-not-exist",44100,2,0));assert(!decoder.is_open());
        std::cout<<"PASS media: "<<argv[i]<<" PCM bytes="<<got<<" nonzero="<<nonzero<<'\n';
    }
}
