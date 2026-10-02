#include "../src/synth.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace {
bool watchAllocations=false;
std::size_t allocations=0;
}
void* operator new(std::size_t bytes) {
    if(watchAllocations)++allocations;
    if(void* pointer=std::malloc(bytes?bytes:1))return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* pointer) noexcept {std::free(pointer);}
void operator delete[](void* pointer) noexcept {std::free(pointer);}
void operator delete(void* pointer,std::size_t) noexcept {std::free(pointer);}
void operator delete[](void* pointer,std::size_t) noexcept {std::free(pointer);}

namespace {
void require(bool condition,const char* message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::abort();}}
double energy(const std::vector<float>& samples,unsigned channel=2) {
    double result=0;for(std::size_t i=channel==2?0:channel;i<samples.size();i+=channel==2?1:2)result+=double(samples[i])*samples[i];
    return result/double(samples.size());
}
void finite(const std::vector<float>& samples) {
    for(float sample:samples)require(std::isfinite(sample)&&std::abs(sample)<1,"nonfinite or unbounded world bus");
}
mc::WorldAudioState engineState(unsigned count=1) {
    mc::WorldAudioState state;state.epoch=1;state.publicationSerial=1;state.engineCount=count;
    for(unsigned i=0;i<count;++i)state.engines[i]={100u+i,.7f,.7f,float(10+i),.5f,int(i%4)};
    return state;
}
mc::WorldAudioState fullState() {
    auto state=engineState(8);state.footCount=8;
    for(unsigned i=0;i<8;++i)state.feet[i]={200u+i,10,.6f,.7f,0,.9f,mc::FootSurface(i%5),int(i%2)};
    return state;
}
void snapshotBasics() {
    static_assert(std::is_trivially_copyable_v<mc::WorldAudioState>);
    static_assert(std::is_trivially_copyable_v<mc::AudioState>);
    std::vector<float> left(9600),right(left.size()),center(left.size()),far(left.size());
    auto state=engineState();state.engines[0].left=1;state.engines[0].right=0;
    mc::WorldSynth a,b,c,d;a.update(state);a.render(left.data(),left.size()/2);
    std::swap(state.engines[0].left,state.engines[0].right);b.update(state);b.render(right.data(),right.size()/2);
    state.engines[0].left=state.engines[0].right=.70710678f;c.update(state);c.render(center.data(),center.size()/2);
    state.engines[0].left=state.engines[0].right=.15f;d.update(state);d.render(far.data(),far.size()/2);
    require(energy(left,0)>0&&energy(left,1)==0,"left pan leaks right");
    for(std::size_t i=0;i<left.size();i+=2)require(left[i]==right[i+1]&&left[i+1]==right[i],"mirror pan differs");
    require(energy(center,0)==energy(center,1),"center pan is not balanced");
    require(energy(far)<energy(center)*.1,"attenuation does not lower power");
    finite(left);finite(center);
}
void serialsAndReadmission() {
    mc::WorldAudioState state;state.epoch=7;state.publicationSerial=1;state.footCount=1;
    state.feet[0]={1,40,1,1,0,1,mc::FootSurface::Pavement,0};
    mc::WorldSynth synth;std::vector<float> samples(960);
    synth.update(state);synth.render(samples.data(),480);
    require(energy(samples)==0&&synth.stats().strikes==0,"first admission replayed a contact");
    ++state.publicationSerial;++state.feet[0].strikeSerial;synth.update(state);synth.render(samples.data(),480);
    require(energy(samples)>0&&synth.stats().strikes==1,"new actual contact was not sounded");
    for(int i=0;i<4;++i){synth.update(state);synth.render(samples.data(),480);}
    require(synth.stats().strikes==1,"duplicate publication replayed a strike");
    ++state.publicationSerial;state.feet[0].strikeSerial=500;synth.update(state);
    require(synth.stats().strikes==2,"serial gap did not retain only the newest strike");
    ++state.publicationSerial;++state.feet[0].strikeSerial;state.feet[0].strikeAgeSeconds=.2f;synth.update(state);
    require(synth.stats().strikes==2,"old strike was caught up");
    ++state.publicationSerial;state.footCount=0;synth.update(state);synth.render(samples.data(),480);
    ++state.publicationSerial;state.footCount=1;++state.feet[0].strikeSerial;state.feet[0].strikeAgeSeconds=0;synth.update(state);
    require(synth.stats().strikes==2,"readmission during tail replayed a strike");
    ++state.publicationSerial;++state.feet[0].strikeSerial;synth.update(state,true);
    ++state.publicationSerial;++state.feet[0].strikeSerial;synth.update(state,false);
    require(synth.stats().strikes==2,"pause/resume replayed a strike");
    ++state.publicationSerial;state.advancing=false;++state.feet[0].strikeSerial;synth.update(state);
    ++state.publicationSerial;state.advancing=true;++state.feet[0].strikeSerial;synth.update(state);
    require(synth.stats().strikes==2,"simulation resume replayed a strike");
    ++state.epoch;state.publicationSerial=1;++state.feet[0].strikeSerial;synth.update(state);
    require(synth.stats().strikes==2,"load epoch replayed a strike");
    for(int i=0;i<5;++i){++state.publicationSerial;++state.feet[0].strikeSerial;synth.update(state);}
    require(synth.stats().droppedStrikes>0&&synth.stats().contacts<=mc::WorldSynth::FootVoices*mc::WorldSynth::ContactsPerFoot,"contact bank did not remain bounded");
    auto older=state;older.publicationSerial=0;older.feet[0].strikeSerial=10000;synth.update(older);
    require(synth.stats().strikes==5,"older publication changed contact history");
}
void expiryAndPause() {
    auto state=engineState();mc::WorldSynth synth;std::vector<float> samples(48000);
    synth.update(state);synth.render(samples.data(),samples.size()/2);
    synth.update(state);synth.render(samples.data(),samples.size()/2);
    require(energy(samples)==0&&synth.stats().engineVoices==0,"stale source did not retire");
    state.advancing=false;synth.update(state);synth.render(samples.data(),samples.size()/2);
    require(energy(samples)==0,"same-serial advancing change revived stale sound");
    synth.update(state,true);synth.update(state,false);synth.render(samples.data(),samples.size()/2);
    require(energy(samples)==0,"same-serial pause change revived stale sound");
    ++state.publicationSerial;synth.update(state);synth.render(samples.data(),4800);
    require(synth.stats().engineVoices==1&&energy(samples)>0,"fresh engine failed to return");
    ++state.publicationSerial;synth.update(state,true);synth.render(samples.data(),samples.size()/2);
    double tail=0;for(std::size_t i=samples.size()-4000;i<samples.size();++i)tail+=samples[i]*samples[i];
    require(tail==0,"paused engine did not silence");
    require(synth.stats().staleReleases==2,"duplicate snapshots refreshed freshness clock");
    mc::Synth full;mc::AudioState initial;initial.station=0;initial.paused=true;initial.world=engineState();
    full.update(initial);full.render(samples.data(),samples.size()/2);full.render(samples.data(),samples.size()/2);
    initial.paused=false;full.update(initial);full.render(samples.data(),4800);
    require(energy(samples)==0,"already-silent pause preserved a stale engine snapshot");
}
void partitionAndOrder() {
    auto state=fullState();auto reversed=state;
    std::reverse(reversed.engines.begin(),reversed.engines.end());std::reverse(reversed.feet.begin(),reversed.feet.end());
    mc::WorldSynth a,b;std::vector<float> one(800),many(800);
    for(unsigned update=0;update<160;++update) {
        ++state.publicationSerial;state.engines[0].speed=float(update%70);
        if(update%16==0)for(auto& foot:state.feet)++foot.strikeSerial;
        if(update==60)for(auto& motor:state.engines)motor.id+=1000;
        if(update==100)for(auto& foot:state.feet)foot.id+=1000;
        reversed=state;std::reverse(reversed.engines.begin(),reversed.engines.end());std::reverse(reversed.feet.begin(),reversed.feet.end());
        a.update(state);b.update(reversed);a.render(one.data(),400);
        for(std::size_t at=0;at<400;){const auto count=std::min<std::size_t>(37,400-at);b.update(reversed);b.render(many.data()+at*2,count);at+=count;}
        require(one==many,"source ordering or buffer partition altered samples");finite(one);
    }
}
void boundariesAndMaterials() {
    std::array<std::vector<float>,5> surfaces;
    for(unsigned material=0;material<5;++material) {
        mc::WorldSynth synth;mc::WorldAudioState state;state.epoch=1;state.publicationSerial=1;state.footCount=1;
        state.feet[0]={21,0,1,1,0,1,mc::FootSurface(material),0};
        synth.update(state);state.publicationSerial=2;state.feet[0].strikeSerial=1;synth.update(state);
        surfaces[material].resize(19200);synth.render(surfaces[material].data(),9600);finite(surfaces[material]);
        require(energy(surfaces[material])>1e-8,"surface contact was inaudible");
        for(unsigned other=0;other<material;++other)require(surfaces[other]!=surfaces[material],"surfaces share identical waveform");
    }
    auto state=fullState();state.engineCount=0;state.footCount=1;mc::WorldSynth a,b;
    std::vector<float> one(4800),two(4800);a.update(state);b.update(state);++state.publicationSerial;++state.feet[0].strikeSerial;
    a.update(state);b.update(state);a.render(one.data(),2400);b.render(two.data(),2400);
    ++state.publicationSerial;a.update(state);state.feet[0].surface=mc::FootSurface::Wood;b.update(state);
    a.render(one.data(),2400);b.render(two.data(),2400);require(one==two,"material update changed a sounding tail");
    mc::WorldSynth transition;state=engineState();transition.update(state);transition.render(one.data(),2400);
    float previous=one[one.size()-2];++state.publicationSerial;state.engines[0].id=900;transition.update(state);transition.render(two.data(),2400);
    require(std::abs(two[0]-previous)<.02f,"source replacement caused a hard sample jump");
    ++state.publicationSerial;state.engineCount=0;transition.update(state);previous=two[two.size()-2];transition.render(one.data(),2400);
    require(std::abs(one[0]-previous)<.02f,"source removal caused a hard sample jump");
}
void invalidAndChurn() {
    auto state=fullState();mc::WorldSynth synth;std::vector<float> samples(128);
    for(unsigned step=0;step<1000;++step) {
        ++state.publicationSerial;
        for(unsigned i=0;i<8;++i){state.engines[i].id=100+step*8+i;state.feet[i].id=10000+step*8+i;++state.feet[i].strikeSerial;}
        synth.update(state);synth.render(samples.data(),64);finite(samples);
        const auto stats=synth.stats();require(stats.engineVoices<=12&&stats.footVoices<=12&&stats.contacts<=36,"source churn exceeded fixed bank");
    }
    require(synth.stats().droppedSources>0,"full voice banks never applied bounded admission");
    state=fullState();state.publicationSerial=2000;state.engineCount=999;state.footCount=999;
    state.engines[0].left=std::numeric_limits<float>::quiet_NaN();state.engines[1].right=std::numeric_limits<float>::infinity();
    state.engines[2].kind=99;state.engines[3].speed=std::numeric_limits<float>::infinity();state.engines[4].id=0;
    state.engines[5].id=state.engines[6].id;state.engines[7].speed=std::numeric_limits<float>::max();
    state.feet[0].strikeAgeSeconds=-1;state.feet[1].strikeAgeSeconds=std::numeric_limits<float>::quiet_NaN();
    state.feet[2].surface=mc::FootSurface(99);state.feet[3].side=-1;state.feet[4].strength=std::numeric_limits<float>::infinity();
    state.feet[5].id=state.feet[6].id;state.feet[7].id=0;
    synth.update(state);synth.render(samples.data(),64);finite(samples);require(synth.stats().rejectedSources>=17,"malformed sources escaped rejection");
    mc::WorldSynth capacity;state=fullState();state.engineCount=0;capacity.update(state);
    for(int event=0;event<3;++event){++state.publicationSerial;for(auto& foot:state.feet)++foot.strikeSerial;capacity.update(state);}
    capacity.render(samples.data(),64);
    ++state.publicationSerial;for(unsigned i=0;i<4;++i)state.feet[i].id+=10000;capacity.update(state);
    for(int event=0;event<3;++event){++state.publicationSerial;for(auto& foot:state.feet)++foot.strikeSerial;capacity.update(state);}
    require(capacity.stats().footVoices==12&&capacity.stats().contacts==36,"full contact-bank stress was not reached");
    capacity.render(samples.data(),64);finite(samples);
}
void ratesAllocationsAndBenchmark() {
    for(unsigned rate:{8000u,44100u,48000u,96000u,384000u}) {
        mc::Synth synth(rate);mc::AudioState audio;audio.world=fullState();audio.station=3;audio.engine=1;audio.rain=1;audio.nature=1;
        audio.footContact=true;audio.footSpeed=7;audio.tireScrub=.8f;audio.speed=35;
        std::vector<float> block(128);const auto began=std::chrono::steady_clock::now();const auto cpuBegan=std::clock();std::uint64_t rendered=0;double checksum=0;
        unsigned peakEngines=0,peakFeet=0,peakContacts=0;
        watchAllocations=true;allocations=0;
        for(unsigned frame=0;frame<360;++frame) {
            ++audio.world.publicationSerial;
            if(frame%3==0)for(auto& foot:audio.world.feet)++foot.strikeSerial;
            if(frame%48==24)for(unsigned i=0;i<4;++i){audio.world.engines[i].id+=100;audio.world.feet[i].id+=100;}
            synth.update(audio);
            // Deliberately harsher than real gait: fill every new contact bank
            // while outgoing walkers still have all their release tails.
            if(frame%48==24)for(int burst=0;burst<3;++burst){++audio.world.publicationSerial;for(auto& foot:audio.world.feet)++foot.strikeSerial;synth.update(audio);}
            const auto stats=synth.worldAudioStats();peakEngines=std::max(peakEngines,stats.engineVoices);
            peakFeet=std::max(peakFeet,stats.footVoices);peakContacts=std::max(peakContacts,stats.contacts);
            unsigned remaining=rate/120;
            while(remaining){const auto count=std::min<unsigned>(remaining,unsigned(block.size()/2));synth.render(block.data(),count);for(unsigned i=0;i<count*2;++i){require(std::isfinite(block[i])&&std::abs(block[i])<1,"sample-rate stress output invalid");checksum+=block[i];}remaining-=count;rendered+=count;}
        }
        watchAllocations=false;require(allocations==0,"real-time update/render allocated");
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        const double cpuSeconds=double(std::clock()-cpuBegan)/CLOCKS_PER_SEC;
        std::printf("World audio worst mix: rate=%u samples=%llu render_wall_ms=%.3f process_cpu_ms=%.3f audio_seconds=%.3f cpu_realtime_fraction=%.5f peak_engines=%u peak_feet=%u peak_contacts=%u checksum=%.6f\n",rate,static_cast<unsigned long long>(rendered),seconds*1000,cpuSeconds*1000,double(rendered)/rate,cpuSeconds/(double(rendered)/rate),peakEngines,peakFeet,peakContacts,checksum);
        std::fflush(stdout);
        require(peakEngines==12&&peakFeet==12&&peakContacts==36,"benchmark failed to exercise full retirement banks");
    }
}
}
int main() {
    snapshotBasics();serialsAndReadmission();expiryAndPause();partitionAndOrder();boundariesAndMaterials();invalidAndChurn();ratesAllocationsAndBenchmark();
    std::puts("World audio: spatial gains, serials, epochs, admission, pause, expiry, deterministic partitions/order, surfaces, transitions, fixed capacities, malformed input, zero allocations and sample rates passed.");
}
