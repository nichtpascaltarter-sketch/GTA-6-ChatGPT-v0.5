#include "../src/synth.h"
#include <algorithm>
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
    for(float x:generate(invalid)) assert(std::isfinite(x) && std::abs(x)<1);
    // A single large render and arbitrarily sized device buffers have identical output.
    mc::Synth continuous,chunked;
    continuous.update(rain);chunked.update(rain);
    std::vector<float> a(96000),b(96000);
    continuous.render(a.data(),48000);
    for(std::size_t i=0;i<48000;) {
        const auto count=std::min<std::size_t>(137,48000-i);
        chunked.render(b.data()+i*2,count);i+=count;
    }
    assert(a==b);
    std::puts("Procedural audio: stations, stereo, finite peaks, silence, engine, weather, siren, shot, pause, gain and buffer determinism passed.");
}
