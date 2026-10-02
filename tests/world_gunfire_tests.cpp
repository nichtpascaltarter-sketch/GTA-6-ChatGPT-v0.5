#include "../src/synth.h"
#include "../src/world_gunfire_scene.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace {bool watching=false;std::size_t allocations=0;}
void* operator new(std::size_t size) {
    if(watching)++allocations;
    if(void* result=std::malloc(size?size:1))return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete[](void* value)noexcept{std::free(value);}
void operator delete(void* value,std::size_t)noexcept{std::free(value);}
void operator delete[](void* value,std::size_t)noexcept{std::free(value);}

namespace {
void require(bool condition,const char* message) {
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::abort();}
}
double energy(const std::vector<float>& data,unsigned channel=2) {
    double sum=0;for(std::size_t i=channel==2?0:channel;i<data.size();i+=channel==2?1:2)sum+=double(data[i])*data[i];
    return sum/double(data.size());
}
void finite(const std::vector<float>& data) {
    for(float sample:data)require(std::isfinite(sample)&&std::abs(sample)<1,"nonfinite or unbounded output");
}
mc::WorldAudioState roster(unsigned count=1) {
    mc::WorldAudioState result;result.epoch=3;result.publicationSerial=1;result.gunCount=count;
    for(unsigned i=0;i<count;++i){auto& gun=result.gunfire[i];gun.shooter=100+i;gun.shotSerial=10;gun.left=gun.right=.70710678f;}
    return result;
}
void fire(mc::WorldAudioState& state) {
    ++state.publicationSerial;
    for(unsigned i=0;i<state.gunCount;++i){auto& gun=state.gunfire[i];++gun.shotSerial;gun.ageSeconds=0;gun.strength=1;}
}
void serialsPayloadsAndPan() {
    static_assert(std::is_trivially_copyable_v<mc::AudioState>);
    auto state=roster();fire(state);mc::WorldSynth synth;std::vector<float> data(960);
    synth.update(state);synth.render(data.data(),480);
    require(energy(data)==0&&synth.stats().gunshots==0,"first admission replayed an emission");
    fire(state);synth.update(state);synth.render(data.data(),480);
    require(energy(data)>0&&synth.stats().gunshots==1,"new emission was not sounded");
    synth.update(state);++state.publicationSerial;synth.update(state);
    require(synth.stats().gunshots==1,"retained emission replayed");
    fire(state);state.gunfire[0].strength=0;synth.update(state);
    ++state.publicationSerial;state.gunfire[0].strength=1;synth.update(state);
    require(synth.stats().gunshots==1,"counter without emission manufactured a later shot");
    fire(state);state.gunfire[0].origin[1]=std::numeric_limits<float>::quiet_NaN();synth.update(state);
    ++state.publicationSerial;state.gunfire[0].origin[1]=0;synth.update(state);
    require(synth.stats().gunshots==1,"malformed event replayed after payload repair");
    fire(state);state.gunfire[0].left=state.gunfire[0].right=0;synth.update(state);
    ++state.publicationSerial;state.gunfire[0].left=1;synth.update(state);
    require(synth.stats().gunshots==1,"out-of-range emission replayed on approach");
    const auto high=state.gunfire[0].shotSerial;
    ++state.publicationSerial;state.gunfire[0].shotSerial=1;synth.update(state);
    ++state.publicationSerial;state.gunfire[0].shotSerial=high;synth.update(state);
    require(synth.stats().gunshots==1,"sequence rollback lowered high-water mark");
    auto old=state;old.publicationSerial=1;old.gunfire[0].shotSerial=1000;synth.update(old);
    fire(state);synth.update(state);require(synth.stats().gunshots==2,"old publication changed the event ledger");
    mc::WorldSynth left,right;auto a=roster();auto b=a;left.update(a);right.update(b);
    fire(a);fire(b);a.gunfire[0].left=b.gunfire[0].right=1;a.gunfire[0].right=b.gunfire[0].left=0;
    left.update(a);right.update(b);std::vector<float> mirror(data.size());left.render(data.data(),480);right.render(mirror.data(),480);
    require(energy(data,0)>0&&energy(data,1)==0,"hard pan leaked between channels");
    for(std::size_t i=0;i<data.size();i+=2)require(data[i]==mirror[i+1]&&data[i+1]==mirror[i],"mirrored pan changed synthesis");
    finite(data);
}
void lifecycleAndPrime() {
    auto state=roster();mc::WorldSynth synth;std::vector<float> data(48000);
    synth.update(state);synth.render(data.data(),15000);fire(state);synth.update(state);
    require(synth.stats().gunshots==0&&synth.stats().staleGunPrimes==1,"silent roster did not expire before fresh event");
    fire(state);synth.update(state);require(synth.stats().gunshots==1,"fresh post-stale event failed");
    ++state.publicationSerial;state.advancing=false;synth.update(state,true);synth.render(data.data(),2000);
    require(synth.stats().gunVoices==0,"pause did not retire voices within release window");
    fire(state);synth.update(state,true);
    ++state.publicationSerial;state.advancing=true;synth.update(state,false);
    require(synth.stats().gunshots==1,"paused counter replayed on resume");
    fire(state);synth.update(state);require(synth.stats().gunshots==2,"fresh resumed emission was suppressed");
    for(unsigned frame=0;frame<20;++frame){++state.publicationSerial;synth.update(state,true);synth.render(data.data(),64);}
    require(synth.stats().gunVoices==0,"repeated paused publications restarted the release deadline");
    ++state.epoch;state.publicationSerial=0;fire(state);synth.update(state);
    require(synth.stats().gunshots==2,"new epoch replayed an emission");
    ++state.publicationSerial;state.gunCount=0;synth.update(state);
    state.gunCount=1;fire(state);synth.update(state);
    require(synth.stats().gunshots==2,"removed shooter replayed on readmission");
    fire(state);synth.update(state);require(synth.stats().gunshots==3,"readmitted shooter never resumed");
    state.gunfire[0].shotSerial=std::numeric_limits<std::uint32_t>::max()-1;++state.publicationSerial;synth.update(state);
    const auto count=synth.stats().gunshots;state.gunfire[0].shotSerial=0;++state.publicationSerial;synth.update(state);
    state.gunfire[0].shotSerial=std::numeric_limits<std::uint32_t>::max()-1;++state.publicationSerial;synth.update(state);
    require(synth.stats().gunshots==count,"unsigned sequence rollover replayed a shot");
    mc::Synth device;mc::AudioState audio;audio.station=0;audio.shot=1;audio.world=roster();fire(audio.world);
    data.assign(960,0);device.prime(audio);device.render(data.data(),480);
    require(energy(data)==0&&device.worldAudioStats().gunshots==0,"device prime replayed a retained shot");
    fire(audio.world);device.update(audio);device.render(data.data(),480);
    require(device.worldAudioStats().gunshots==1,"device prime suppressed later fresh shot");
    fire(audio.world);device.prime(audio);require(device.worldAudioStats().gunshots==1,"explicit reprime replayed on a live synth");
}
void capacityOrderAgeAndTails() {
    mc::WorldSynth full,selected;auto all=roster(16),expected=all;
    for(unsigned i=0;i<16;++i)all.gunfire[i].left=all.gunfire[i].right=float(i+1)/16;
    expected=all;full.update(all);selected.update(expected);fire(all);fire(expected);
    for(unsigned i=0;i<8;++i)expected.gunfire[i].strength=0;
    full.update(all);selected.update(expected);std::vector<float> a(60000),b(a.size());
    full.render(a.data(),480);selected.render(b.data(),480);
    require(a==b&&full.stats().gunshots==8&&full.stats().droppedGunshots==8,"capacity did not choose deterministic loudest eight");
    fire(all);full.update(all);require(full.stats().gunshots==8&&full.stats().droppedGunshots==24,"full bank overwrote audible voice");
    full.render(a.data(),30000);++all.publicationSerial;full.update(all);
    require(full.stats().gunshots==8&&full.stats().gunVoices==0,"capacity-dropped event replayed after tail retired");
    mc::WorldSynth one,many;auto state=roster(16);one.update(state);many.update(state);
    a.resize(800);b.resize(800);
    for(unsigned frame=0;frame<100;++frame) {
        ++state.publicationSerial;
        if(frame%29==0)fire(state);
        if(frame==45)++state.epoch;
        auto reverse=state;std::reverse(reverse.gunfire.begin(),reverse.gunfire.end());
        one.update(state);many.update(reverse);one.render(a.data(),400);
        for(std::size_t at=0;at<400;){const auto n=std::min<std::size_t>(37,400-at);many.update(reverse);many.render(b.data()+at*2,n);at+=n;}
        require(a==b,"source permutation or buffer partition changed gunfire samples");finite(a);
    }
    mc::WorldSynth young,aged,expired;auto y=roster(),o=y,e=y;young.update(y);aged.update(o);expired.update(e);
    fire(y);fire(o);fire(e);o.gunfire[0].ageSeconds=.12f;e.gunfire[0].ageSeconds=.121f;
    young.update(y);aged.update(o);expired.update(e);young.render(a.data(),400);aged.render(b.data(),400);
    require(aged.stats().gunshots==1&&expired.stats().gunshots==0&&energy(b)<energy(a)*.2,"admission age did not trim the attack or reject stale event");
    auto removed=y;++removed.publicationSerial;removed.gunCount=0;young.update(removed);young.render(a.data(),400);
    require(energy(a)>0,"shooter disappearance cut off an emitted tail");
}
mc::LawState lawRoster() {
    mc::LawState law;law.count=3;
    law.units[0].identity=73;law.units[1].identity=12;law.units[2].identity=99;law.units[2].kind=mc::LawUnitKind::Patrol;
    return law;
}
void trackerRetentionAndSuspension() {
    mc::WorldGunfireScene scene;auto law=lawRoster();mc::WorldAudioState output;output.epoch=4;
    mc::WorldSynth synth;std::array<mc::LawShot,1> events{{{73,1,{7,2,4},{0,0,1},45,8}}};
    auto pan=[](mc::Vec3 origin,float& left,float& right){left=origin.x>0?0.f:1.f;right=origin.x>0?1.f:0.f;return true;};
    auto publish=[&](std::span<const mc::LawShot> shots,bool running=true) {
        scene.update(output,law,shots,1.f/60,running,pan);++output.publicationSerial;output.advancing=running;return output;
    };
    synth.update(publish({}));require(output.gunCount==2&&output.gunfire[0].shooter==12,"tracker omitted/reordered officer baselines or included patrol");
    law.units[0].weapon.shotsFired=1;auto emitted=publish(events);require(emitted.gunfire[1].strength==1&&emitted.gunfire[1].ageSeconds==0,"actual event was not retained");
    // The audio consumer misses the emission publication but reads its next frame.
    auto retained=publish({});synth.update(retained);
    require(synth.stats().gunshots==1&&retained.gunfire[1].origin==emitted.gunfire[1].origin&&retained.gunfire[1].ageSeconds>0,"missed publication lost or moved the retained event");
    auto repeated=publish(events);require(repeated.gunfire[1].ageSeconds>retained.gunfire[1].ageSeconds,"repeated Game event reset its age");
    synth.update(publish({},false));require(output.gunfire[1].strength==0,"suspension preserved old event payload");
    synth.update(publish(events));require(synth.stats().gunshots==1,"resume replayed pre-suspension Game event");
    ++law.units[0].weapon.shotsFired;++events[0].sequence;synth.update(publish(events));
    require(synth.stats().gunshots==2,"first genuinely fresh resumed shot was suppressed");
    publish({},false); // Deliberately coalesced away by the audio mailbox.
    synth.update(publish({}));require(synth.stats().gunshots==2,"skipped pause snapshot replayed old event");
    ++law.units[0].weapon.shotsFired;synth.update(publish({}));
    ++events[0].sequence;synth.update(publish(events));require(synth.stats().gunshots==2,"late payload fabricated an already-consumed counter");
    ++law.units[0].weapon.shotsFired;++events[0].sequence;events[0].origin.x=std::numeric_limits<float>::infinity();
    synth.update(publish(events));events[0].origin.x=4;synth.update(publish(events));
    require(synth.stats().gunshots==2,"invalid origin repaired into a delayed shot");
    ++law.units[0].weapon.shotsFired;++events[0].sequence;publish(events);
    for(int i=0;i<30;++i)publish({});
    require(output.gunfire[1].strength==0,"retention exceeded bounded event lifetime");
    scene.reset();++output.epoch;synth.update(publish(events));require(synth.stats().gunshots==2,"scene reset replayed retained Game event");
    watching=true;allocations=0;
    for(unsigned i=0;i<1000;++i){if(i%40==0){++law.units[0].weapon.shotsFired;++events[0].sequence;}publish(events);}
    watching=false;require(allocations==0,"tracker allocated during publication");
}
void malformedAndBenchmark() {
    mc::WorldSynth invalid;auto state=roster(16);invalid.update(state);fire(state);state.gunCount=999;
    state.gunfire[0].left=std::numeric_limits<float>::quiet_NaN();state.gunfire[1].right=-1;
    state.gunfire[2].ageSeconds=std::numeric_limits<float>::infinity();state.gunfire[3].strength=-1;
    state.gunfire[4].shooter=0;state.gunfire[5].shooter=state.gunfire[6].shooter;
    state.gunfire[7].origin[0]=std::numeric_limits<float>::infinity();
    invalid.update(state);std::vector<float> block(128);invalid.render(block.data(),64);finite(block);
    require(invalid.stats().rejectedGunSources>=9,"malformed sources were not rejected");
    mc::WorldSynth dedup;auto duplicate=roster(2);dedup.update(duplicate);
    duplicate.gunfire[1].shooter=duplicate.gunfire[0].shooter;fire(duplicate);dedup.update(duplicate);
    require(dedup.stats().gunshots==0&&dedup.stats().gunShooters==0,"duplicate identities were not rejected together");
    duplicate.gunfire[1].shooter=101;fire(duplicate);dedup.update(duplicate);
    require(dedup.stats().gunshots==0&&dedup.stats().gunShooters==2,"single identity after duplicate failed readmission priming");
    fire(duplicate);dedup.update(duplicate);require(dedup.stats().gunshots==2,"fresh shots after duplicate recovery failed");
    for(unsigned rate:{8000u,44100u,48000u,96000u,192000u,384000u}) {
        mc::Synth synth(rate);mc::AudioState audio;audio.world=roster(16);audio.station=3;audio.engine=1;audio.rain=1;audio.nature=1;
        audio.footContact=true;audio.footSpeed=7;audio.tireScrub=.8f;audio.speed=35;
        audio.world.engineCount=audio.world.footCount=8;
        for(unsigned i=0;i<8;++i){audio.world.engines[i]={200+i,.7f,.7f,30,.5f,int(i%4)};audio.world.feet[i]={400+i,0,.6f,.7f,0,1,mc::FootSurface(i%5),int(i%2)};}
        synth.update(audio);const auto begin=std::chrono::steady_clock::now();const auto cpu=std::clock();
        unsigned peakGuns=0,peakEngines=0,peakFeet=0,peakContacts=0;std::uint64_t rendered=0;double checksum=0;
        watching=true;allocations=0;
        for(unsigned frame=0;frame<360;++frame) {
            ++audio.world.publicationSerial;
            if(frame%3==0)for(auto& foot:audio.world.feet)++foot.strikeSerial;
            if(frame%48==24)for(unsigned i=0;i<4;++i){audio.world.engines[i].id+=100;audio.world.feet[i].id+=100;}
            if(frame%54==1)fire(audio.world);
            synth.update(audio);
            if(frame%48==24)for(int burst=0;burst<3;++burst){++audio.world.publicationSerial;for(auto& foot:audio.world.feet)++foot.strikeSerial;synth.update(audio);}
            auto stats=synth.worldAudioStats();peakGuns=std::max(peakGuns,stats.gunVoices);peakEngines=std::max(peakEngines,stats.engineVoices);
            peakFeet=std::max(peakFeet,stats.footVoices);peakContacts=std::max(peakContacts,stats.contacts);
            unsigned remaining=rate/120;
            while(remaining){const auto count=std::min(remaining,64u);synth.render(block.data(),count);for(unsigned i=0;i<count*2;++i){require(std::isfinite(block[i])&&std::abs(block[i])<1,"stress output invalid");checksum+=block[i];}remaining-=count;rendered+=count;}
        }
        synth.setSampleRate(rate==48000?8000:48000);synth.render(block.data(),64);finite(block);
        watching=false;require(allocations==0,"update/render/rate-change allocated");
        require(peakGuns==8&&peakEngines==12&&peakFeet==12&&peakContacts==36,"benchmark did not reach every bank capacity");
        const double cpuSeconds=double(std::clock()-cpu)/CLOCKS_PER_SEC;
        const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        std::printf("Gunfire worst mix: rate=%u frames=%llu wall_ms=%.3f cpu_ms=%.3f cpu_fraction=%.5f peaks=%u/%u/%u/%u checksum=%.6f\n",rate,static_cast<unsigned long long>(rendered),wall*1000,cpuSeconds*1000,cpuSeconds/(double(rendered)/rate),peakGuns,peakEngines,peakFeet,peakContacts,checksum);std::fflush(stdout);
    }
}
}
int main() {
    serialsPayloadsAndPan();lifecycleAndPrime();capacityOrderAgeAndTails();trackerRetentionAndSuspension();malformedAndBenchmark();
    std::puts("World gunfire: actual emissions, baseline/serials, pan, lifecycle, capacity, ages, deterministic partitions/order, tracker retention, invalid data, zero allocations and all sample rates passed.");
}
