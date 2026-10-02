#include "../src/synth.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
std::vector<float> generate(mc::AudioState state,unsigned seconds=2) {
    mc::Synth synth;
    synth.update(state);
    std::vector<float> result(48000*2*seconds);
    synth.render(result.data(),result.size()/2);
    return result;
}
double energy(const std::vector<float>& data,std::size_t begin=0,std::size_t end=0) {
    if(!end) end=data.size();
    double sum=0;for(std::size_t i=begin;i<end;++i) sum+=data[i]*data[i];
    return sum/static_cast<double>(end-begin);
}
double roughness(const std::vector<float>& data) {
    double signal=0,difference=0;
    for(std::size_t i=2;i<data.size();++i) {
        signal+=data[i]*data[i];
        const double step=data[i]-data[i-2];difference+=step*step;
    }
    return difference/signal;
}
std::vector<int> contactSides(const std::vector<float>& data) {
    // Ten-millisecond energy windows identify audible impacts, not internal DSP state.
    std::vector<double> windows;
    for(std::size_t start=0;start+960<=data.size();start+=960)
        windows.push_back(energy(data,start,start+960));
    const double threshold=*std::max_element(windows.begin(),windows.end())*.16;
    std::vector<int> sides;bool active=false;
    for(std::size_t window=0;window<windows.size();++window) {
        const bool next=windows[window]>threshold;
        if(next&&!active) {
            double left=0,right=0;
            const auto end=std::min(data.size(),window*960+2880);
            for(std::size_t i=window*960;i<end;i+=2) {left+=data[i]*data[i];right+=data[i+1]*data[i+1];}
            sides.push_back(left>right?-1:1);
        }
        active=next;
    }
    return sides;
}
}
int main() {
    mc::AudioState quiet;quiet.station=0;quiet.volume=1;
    const auto silence=generate(quiet);
    assert(energy(silence)==0);
    std::vector<float> prior;
    for(int station=1;station<=3;++station) {
        mc::AudioState state=quiet;state.station=station;
        auto samples=generate(state,12);
        assert(energy(samples)>0.0001);
        double mean=0,stereo=0;
        for(std::size_t i=0;i<samples.size();++i) {
            assert(std::isfinite(samples[i]) && std::abs(samples[i])<1);
            mean+=samples[i];
            if(i%2==0) stereo+=std::abs(samples[i]-samples[i+1]);
        }
        assert(std::abs(mean/samples.size())<0.004);
        assert(stereo/samples.size()>0.001);
        if(!prior.empty()) assert(prior!=samples);
        prior=std::move(samples);
    }
    mc::AudioState engine=quiet;engine.engine=1;
    auto idle=generate(engine);engine.speed=30;auto fast=generate(engine);
    assert(energy(idle)>0.001 && energy(fast)>0.001 && idle!=fast);
    prior=fast;
    for(int kind=1;kind<=3;++kind){
        engine.engineKind=kind;engine.throttle=.8f;auto samples=generate(engine);
        assert(energy(samples)>.0005&&samples!=prior);
        for(float value:samples)assert(std::isfinite(value)&&std::fabs(value)<1);
        prior=std::move(samples);
    }
    mc::AudioState coast=quiet;coast.shore=1;
    mc::AudioState nature=quiet;nature.nature=1;
    mc::AudioState city=quiet;city.urban=1;
    const auto surf=generate(coast),wildlife=generate(nature),traffic=generate(city);
    assert(energy(surf)>.000001&&energy(wildlife)>.0000001&&energy(traffic)>.0000001);
    assert(surf!=wildlife&&wildlife!=traffic);

    mc::AudioState walking=quiet;walking.footSpeed=3.9f;walking.footContact=true;
    std::array<std::vector<float>,5> surfaces;
    for(int surface=0;surface<5;++surface) {
        walking.footSurface=static_cast<mc::FootSurface>(surface);
        surfaces[surface]=generate(walking,4);
        assert(energy(surfaces[surface])>.000008);
        for(float x:surfaces[surface])assert(std::isfinite(x)&&std::abs(x)<1);
        if(surface)assert(surfaces[surface]!=surfaces[surface-1]);
    }
    // Hard soles contain a sharper impact than yielding ground; planks ring.
    assert(roughness(surfaces[0])>roughness(surfaces[2])*2);
    assert(roughness(surfaces[2])>roughness(surfaces[3])*2);
    assert(roughness(surfaces[3])>roughness(surfaces[4])*5);
    const auto walkingSides=contactSides(surfaces[0]);
    assert(walkingSides.size()>=8&&walkingSides.size()<=11);
    for(std::size_t i=1;i<walkingSides.size();++i)assert(walkingSides[i]!=walkingSides[i-1]);
    walking.footSurface=mc::FootSurface::Pavement;walking.footSpeed=7.3f;
    const auto running=generate(walking,4);
    assert(contactSides(running).size()>walkingSides.size());
    assert(energy(running)>energy(surfaces[0])*1.5);
    walking.footContact=false;
    assert(energy(generate(walking))==0); // Moving through air does not strike the ground.
    walking.footContact=true;walking.footSpeed=0;
    assert(energy(generate(walking))==0); // Standing or pushing against a wall is silent.

    mc::AudioState swimming=quiet;swimming.waterMotion=.2f;
    const auto slowSwim=generate(swimming);swimming.waterMotion=.85f;
    const auto fastSwim=generate(swimming);
    assert(energy(slowSwim)>.0000001&&energy(fastSwim)>energy(slowSwim)*4);
    assert(fastSwim!=surfaces[0]);
    mc::AudioState sliding=quiet;sliding.speed=30;sliding.tireScrub=.25f;
    const auto smallSlide=generate(sliding);sliding.tireScrub=.85f;
    assert(energy(generate(sliding))>energy(smallSlide)*4);
    sliding.speed=0;
    assert(energy(generate(sliding))==0); // No tire squeal from stationary braking.
    sliding.speed=30;
    for(int kind=2;kind<=3;++kind) {
        sliding.engineKind=kind;const auto withoutTires=generate(sliding);
        sliding.tireScrub=0;assert(withoutTires==generate(sliding));sliding.tireScrub=.85f;
    }

    // An in-flight contact tail keeps its original material until the next strike.
    mc::Synth unchangedSurface,changedSurface;walking.footSpeed=3.9f;
    unchangedSurface.update(walking);changedSurface.update(walking);
    std::vector<float> surfaceA(6720),surfaceB(6720);
    unchangedSurface.render(surfaceA.data(),3360);changedSurface.render(surfaceB.data(),3360);
    walking.footSurface=mc::FootSurface::Wood;changedSurface.update(walking);
    unchangedSurface.render(surfaceA.data(),3360);changedSurface.render(surfaceB.data(),3360);
    assert(surfaceA==surfaceB&&energy(surfaceA)>0);
    mc::AudioState rain=quiet;rain.rain=1;
    assert(energy(generate(rain))>0.0002);
    mc::AudioState wanted=quiet;wanted.wanted=3;
    assert(energy(generate(wanted))>0.0003);
    mc::AudioState shot=quiet;shot.shot=1;
    auto blast=generate(shot);
    assert(energy(blast,0,12000)>0.00002);
    assert(energy(blast,96000)==0);
    mc::Synth synth;synth.update(engine);
    std::vector<float> buffer(96000);
    synth.render(buffer.data(),48000);
    engine.paused=true;synth.update(engine);synth.render(buffer.data(),48000);
    assert(energy(buffer,48000)==0);
    engine.paused=false;engine.volume=0;synth.update(engine);synth.render(buffer.data(),48000);
    assert(energy(buffer,48000)==0);
    engine.volume=1;synth.update(engine);synth.render(buffer.data(),48000);
    assert(energy(buffer,48000)>0.001);
    mc::AudioState invalid;
    invalid.speed=std::numeric_limits<float>::infinity();
    invalid.rain=std::numeric_limits<float>::quiet_NaN();
    invalid.wanted=-20;invalid.volume=8;invalid.station=999;
    invalid.engineKind=999;invalid.throttle=std::numeric_limits<float>::quiet_NaN();
    invalid.shore=std::numeric_limits<float>::infinity();invalid.nature=-30;invalid.urban=10;
    invalid.footSurface=static_cast<mc::FootSurface>(999);
    invalid.footContact=true;invalid.footSpeed=std::numeric_limits<float>::quiet_NaN();
    invalid.waterMotion=std::numeric_limits<float>::infinity();invalid.tireScrub=-10;
    for(float x:generate(invalid)) assert(std::isfinite(x) && std::abs(x)<1);
    // A single large render and arbitrarily sized device buffers have identical output.
    mc::Synth continuous,chunked;
    rain.shore=.8f;rain.nature=.7f;rain.urban=.4f;rain.engine=1;rain.engineKind=0;rain.throttle=.9f;
    rain.footContact=true;rain.footSpeed=7.3f;rain.footSurface=mc::FootSurface::Soil;
    rain.waterMotion=.8f;rain.tireScrub=.7f;rain.speed=30;
    continuous.update(rain);chunked.update(rain);
    std::vector<float> a(96000),b(96000);
    continuous.render(a.data(),48000);
    for(std::size_t i=0;i<48000;) {
        const auto count=std::min<std::size_t>(137,48000-i);
        chunked.update(rain); // Repeated state publication must not restart contacts.
        chunked.render(b.data()+i*2,count);i+=count;
    }
    assert(a==b);
    // Switching districts and engine types must not introduce a hard sample jump.
    mc::Synth transition;transition.update(engine);transition.render(a.data(),48000);
    float previous=a.back();transition.update(coast);transition.render(b.data(),48000);
    assert(std::fabs(b.front()-previous)<.08f);
    for(float value:b)assert(std::isfinite(value)&&std::fabs(value)<1);

    mc::Synth contacts;contacts.update(rain);contacts.render(a.data(),48000);
    rain.paused=true;contacts.update(rain);contacts.render(b.data(),48000);
    assert(energy(b,48000)==0);
    rain.paused=false;contacts.update(rain);contacts.render(a.data(),48000);
    assert(energy(a,48000)>.001);
    contacts.update(quiet);contacts.render(b.data(),48000);
    assert(energy(b,72000)<.000000000001); // Contact tails and continuous friction retire.

    // A new full skid fades in instead of replacing the waveform at a block edge.
    mc::Synth skidTransition;skidTransition.update(quiet);skidTransition.render(a.data(),48000);
    sliding.engineKind=0;sliding.tireScrub=1;skidTransition.update(sliding);
    skidTransition.render(b.data(),48000);assert(std::abs(b.front())<.001);
    for(unsigned rate:{8000u,44100u,96000u,384000u}) {
        mc::Synth device(rate);device.update(rain);std::vector<float> samples(rate*2);
        device.render(samples.data(),rate);
        assert(energy(samples)>.0001);
        for(float value:samples)assert(std::isfinite(value)&&std::fabs(value)<1);
    }
    std::puts("Procedural audio: stations, stereo, finite peaks, silence, four engines, ambience, five contact surfaces, alternating steps/cadence, water strokes, tire scrub/gating, transitions, weather, siren, shot, pause, gain, device rates and buffer determinism passed.");
}
