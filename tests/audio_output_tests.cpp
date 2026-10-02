#include "../src/audio_output_format.h"
#include "../src/audio_mailbox.h"
#include "../src/synth.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
#include <vector>

namespace {
void require(bool condition,const char* message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::abort();}}
std::uint32_t read(const std::uint8_t* data,unsigned bytes) {
    std::uint32_t value=0;for(unsigned i=0;i<bytes;++i)value|=std::uint32_t(data[i])<<(i*8);return value;
}
void exactPcmSamples() {
    const std::array<float,12> stereo{{-1,-1,-.5f,-.5f,0,0,.5f,.5f,1,1,2,-2}};
    const std::array<std::array<std::uint32_t,12>,4> expected{{
        {{0,0,64,64,128,128,192,192,255,255,255,0}},
        {{0x8000,0x8000,0xc000,0xc000,0,0,0x4000,0x4000,0x7fff,0x7fff,0x7fff,0x8000}},
        {{0x800000,0x800000,0xc00000,0xc00000,0,0,0x400000,0x400000,0x7fffff,0x7fffff,0x7fffff,0x800000}},
        {{0x80000000,0x80000000,0xc0000000,0xc0000000,0,0,0x40000000,0x40000000,0x7fffffff,0x7fffffff,0x7fffffff,0x80000000}}
    }};
    for(unsigned size=1;size<=4;++size) {
        std::array<std::uint8_t,64> packed{};packed.fill(0x6d);
        const mc::AudioOutputFormat format{2,size*8,size*8,false};
        require(mc::packAudioFrames(packed.data()+1,stereo.data(),6,format),"supported PCM format was rejected");
        for(unsigned i=0;i<12;++i)require(read(packed.data()+1+i*size,size)==expected[size-1][i],"PCM sample or clipping endpoint has incorrect bytes");
        require(packed.front()==0x6d&&packed[1+12*size]==0x6d,"PCM write crossed output frame boundary");
    }
}
void validBitAlignment() {
    std::array<std::uint8_t,32> packed{};
    const float lsb=1.f/8388608.f;
    const std::array<float,8> stereo{{-1,1,lsb,-lsb,0,0,.5f,-.5f}};
    require(mc::packAudioFrames(packed.data(),stereo.data(),4,{2,32,24,false}),"24-in-32 PCM rejected");
    const std::array<std::uint32_t,8> expected{{0x80000000,0x7fffff00,0x100,0xffffff00,0,0,0x40000000,0xc0000000}};
    for(unsigned i=0;i<expected.size();++i)require(read(packed.data()+4*i,4)==expected[i],"valid bits were not quantized before left alignment");
    const std::array<float,6> smaller{{-1,1,0,0,.5f,-.5f}};
    require(mc::packAudioFrames(packed.data(),smaller.data(),3,{2,8,4,false}),"four-bit precision in unsigned PCM rejected");
    const std::array<std::uint8_t,6> expected8{{0,240,128,128,192,64}};
    require(std::equal(expected8.begin(),expected8.end(),packed.begin()),"unsigned PCM valid-bit alignment or midpoint is wrong");
    for(unsigned container:{8u,16u,24u,32u})for(unsigned valid=1;valid<=container;++valid) {
        require(mc::packAudioFrames(packed.data(),smaller.data(),3,{2,container,valid,false}),"valid PCM precision rejected");
        const std::uint32_t mask=(std::uint32_t(1)<<(container-valid))-1;
        for(unsigned i=0;i<6;++i)require((read(packed.data()+i*(container/8),container/8)&mask)==0,"unused low PCM bits were not zero");
    }
}
void floatMonoAndSurround() {
    const std::array<float,6> stereo{{.75f,-.25f,1,-1,0,0}};
    std::array<std::uint8_t,128> packed{};packed.fill(0xc3);
    require(mc::packAudioFrames(packed.data()+1,stereo.data(),3,{1,32,32,true}),"float mono rejected");
    for(unsigned i=0;i<3;++i)require(read(packed.data()+1+i*4,4)==std::bit_cast<std::uint32_t>(i==0?.25f:0.f),"mono downmix is incorrect");
    require(packed[0]==0xc3&&packed[13]==0xc3,"unaligned float output crossed boundary");
    for(bool floating:{false,true}) {
        const unsigned bits=floating?32:8;
        require(mc::packAudioFrames(packed.data(),stereo.data(),3,{6,bits,bits,floating}),"surround format rejected");
        const unsigned bytes=bits/8;
        for(unsigned frame=0;frame<3;++frame)for(unsigned channel=2;channel<6;++channel)
            require(read(packed.data()+(frame*6+channel)*bytes,bytes)==(floating?0u:128u),"unused surround channel is not digital silence");
    }
    require(mc::packAudioFrames(packed.data(),stereo.data(),3,{2,32,32,true}),"float stereo rejected");
    for(unsigned i=0;i<stereo.size();++i)require(read(packed.data()+i*4,4)==std::bit_cast<std::uint32_t>(stereo[i]),"float output changed a valid sample");
}
void malformedInput() {
    std::array<std::uint8_t,32> packed{};packed.fill(0x8b);
    const std::array<mc::AudioOutputFormat,9> invalid{{{0,16,16,false},{33,16,16,false},{2,12,12,false},{2,16,0,false},
        {2,16,17,false},{2,64,64,true},{2,32,24,true},{2,24,24,true},{2,0,0,false}}};
    const std::array<float,8> stereo{{std::numeric_limits<float>::quiet_NaN(),0,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),.5f,-.5f}};
    for(const auto& format:invalid) {
        require(!mc::packAudioFrames(packed.data(),stereo.data(),4,format),"malformed format was accepted");
        require(std::all_of(packed.begin(),packed.end(),[](auto byte){return byte==0x8b;}),"malformed format wrote output");
    }
    require(mc::packAudioFrames(packed.data(),stereo.data(),4,{2,32,32,true}),"finite-boundary test format rejected");
    const std::array<float,8> expected{{0,0,1,-1,1,-1,.5f,-.5f}};
    for(unsigned i=0;i<expected.size();++i)require(read(packed.data()+i*4,4)==std::bit_cast<std::uint32_t>(expected[i]),"nonfinite output sample not sanitized");
    require(mc::packAudioFrames(packed.data(),stereo.data()+4,1,{1,32,32,true}),"large finite mono rejected");
    require(read(packed.data(),4)==0,"large finite mono overflowed instead of cancelling");
}
void reopenEventBaseline() {
    mc::AudioState state;state.station=0;state.volume=1;state.shot=1;
    std::vector<float> samples(48000);
    mc::Synth reopened;reopened.prime(state);reopened.render(samples.data(),samples.size()/2);
    require(std::all_of(samples.begin(),samples.end(),[](float value){return value==0;}),"device reopen replayed a historical shot");
    state.shot=.4f;reopened.update(state);reopened.render(samples.data(),256);
    state.shot=1;reopened.update(state);reopened.render(samples.data(),256);
    require(std::any_of(samples.begin(),samples.begin()+512,[](float value){return value!=0;}),"new post-reopen shot was suppressed");
    mc::Synth ordinary;ordinary.update(state);ordinary.render(samples.data(),256);
    require(std::any_of(samples.begin(),samples.begin()+512,[](float value){return value!=0;}),"ordinary initial shot behavior changed");
    mc::Synth delayed;mc::AudioState waiting;waiting.station=0;waiting.paused=true;delayed.update(waiting);delayed.render(samples.data(),256);
    delayed.prime(state);delayed.render(samples.data(),samples.size()/2);
    require(std::all_of(samples.begin(),samples.end(),[](float value){return value==0;}),"first real snapshot after silent handoff replayed a historical shot");
}
void mailboxPublication() {
    mc::AudioMailbox mailbox;mc::AudioState latest;latest.station=0;latest.paused=true;
    require(!mailbox.tryRead(latest)&&latest.station==0&&latest.paused,"unpublished mailbox replaced silent fallback");
    std::atomic<bool> ready=false,finished=false;
    std::thread publisher([&] {
        while(!ready.load())std::this_thread::yield();
        mc::AudioState state;
        for(std::uint64_t serial=1;serial<=30000;++serial) {
            state.world.publicationSerial=serial;state.speed=float(serial);state.station=int(serial%4);
            for(auto& engine:state.world.engines){engine.id=serial;engine.left=float(serial);}
            for(auto& foot:state.world.feet){foot.id=serial;foot.strikeSerial=serial;}
            mailbox.publish(state);
        }
        finished=true;
    });
    ready=true;std::uint64_t previous=0;
    auto inspect=[&] {
        const auto serial=latest.world.publicationSerial;
        require(serial>=previous&&latest.speed==float(serial)&&latest.station==int(serial%4),"mailbox copied a torn control snapshot");
        for(const auto& engine:latest.world.engines)require(engine.id==serial&&engine.left==float(serial),"mailbox tore an engine source");
        for(const auto& foot:latest.world.feet)require(foot.id==serial&&foot.strikeSerial==serial,"mailbox tore a foot source");
        previous=serial;
    };
    while(!finished.load()){if(mailbox.tryRead(latest))inspect();else std::this_thread::yield();}
    publisher.join();require(mailbox.tryRead(latest),"published mailbox never delivered a snapshot");inspect();
    require(previous==30000,"mailbox lost the final publication");
}
}
int main() {
    exactPcmSamples();validBitAlignment();floatMonoAndSurround();malformedInput();reopenEventBaseline();mailboxPublication();
    std::puts("Audio output: exact PCM endpoints/midpoints, all valid-bit precisions, float identity, mono/surround silence, bounded writes, malformed formats, finite clipping, device-reopen shot priming and coherent publication passed.");
}
