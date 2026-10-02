#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mc {
struct AudioOutputFormat {
    unsigned channels=0,bits=0,validBits=0;
    bool floating=false;
};
inline bool validAudioOutputFormat(const AudioOutputFormat& format) {
    if(format.channels<1||format.channels>32||format.validBits<1||format.validBits>format.bits)return false;
    if(format.floating)return format.bits==32&&format.validBits==32;
    return format.bits==8||format.bits==16||format.bits==24||format.bits==32;
}
inline float audioOutputSample(float sample) {
    return std::isnan(sample)?0.f:std::clamp(sample,-1.f,1.f);
}

// Windows PCM stores unsigned eight-bit samples and signed little-endian
// samples in larger containers. Extensible valid bits are left-aligned.
// The remaining channels of a multichannel endpoint receive digital silence.
inline bool packAudioFrames(std::uint8_t* output,const float* stereo,std::size_t frames,
                            const AudioOutputFormat& format) {
    if(!validAudioOutputFormat(format))return false;
    const unsigned bytes=format.bits/8,padding=format.bits-format.validBits;
    const std::int64_t scale=std::int64_t(1)<<(format.validBits-1);
    for(std::size_t frame=0;frame<frames;++frame) {
        const float left=audioOutputSample(stereo[frame*2]),right=audioOutputSample(stereo[frame*2+1]);
        for(unsigned channel=0;channel<format.channels;++channel) {
            float sample=0;
            if(format.channels==1)sample=left*.5f+right*.5f;
            else if(channel<2)sample=channel==0?left:right;
            auto* destination=output+(frame*format.channels+channel)*bytes;
            if(format.floating){std::memcpy(destination,&sample,sizeof(sample));continue;}
            const double scaled=double(sample)*double(scale);
            auto quantized=static_cast<std::int64_t>(scaled+(scaled<0?-.5:.5));
            quantized=std::clamp(quantized,-scale,scale-1);
            if(bytes==1)quantized+=scale;
            const auto packed=static_cast<std::uint32_t>(quantized)<<padding;
            for(unsigned byte=0;byte<bytes;++byte)destination[byte]=static_cast<std::uint8_t>(packed>>(byte*8));
        }
    }
    return true;
}
}
