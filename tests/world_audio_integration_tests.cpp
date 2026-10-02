#include "../src/audio_scene.h"
#include "../src/synth.h"
#include <array>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){
    try{
        mc::Game game;game.initialize();game.dayTime=8;
        mc::WorldAudioScene scene;uint64_t epoch=1;scene.reset(epoch);
        mc::Synth synth(48000);mc::AudioState state;state.station=0;
        std::array<float,1600> samples{};
        uint64_t rendered=0,fingerprint=1469598103934665603ull;
        unsigned maximumEngines=0,maximumFeet=0;double energy=0;float peak=0;
        auto publish=[&](bool advancing){
            const auto eye=game.cameraEye();
            state.world=scene.update(game,eye,game.cameraTarget()-eye,advancing?1.f/60:0,advancing,epoch);
            state.paused=game.paused;
            maximumEngines=std::max(maximumEngines,state.world.engineCount);
            maximumFeet=std::max(maximumFeet,state.world.footCount);
            require(state.world.engineCount<=8&&state.world.footCount<=8,"source bounds exceeded");
            synth.update(state);synth.render(samples.data(),800);rendered+=800;
            for(float sample:samples){
                require(std::isfinite(sample)&&std::abs(sample)<=1,"invalid synthesized sample");
                energy+=double(sample)*sample;peak=std::max(peak,std::abs(sample));
                fingerprint^=std::bit_cast<uint32_t>(sample);fingerprint*=1099511628211ull;
            }
        };
        publish(false);
        for(int frame=0;frame<600;++frame){game.update({},1.f/60,false);publish(true);}
        require(maximumEngines>0&&maximumFeet>0,"live population produced no spatial sources");
        require(scene.stats().contacts>0&&synth.worldAudioStats().strikes>0,"live movement produced no audible contacts");
        const auto contacts=scene.stats().contacts;
        game.paused=true;
        for(int frame=0;frame<30;++frame)publish(false);
        require(scene.stats().contacts==contacts,"suspension created a new contact");
        for(float sample:samples)require(std::abs(sample)<.000001f,"suspended stream did not become silent");
        game.paused=false;game.update({},1.f/60,false);
        auto strikes=synth.worldAudioStats().strikes;publish(true);
        require(synth.worldAudioStats().strikes==strikes,"resume replayed a historical footstep");
        for(int frame=0;frame<300;++frame){game.update({},1.f/60,false);publish(true);}
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path=std::filesystem::temp_directory_path()/("meridian-live-audio-"+std::to_string(stamp)+".sav");
        const auto utf8=path.u8string();const std::string savePath(utf8.begin(),utf8.end());
        require(game.save(savePath),"live scene save failed");
        const bool loaded=game.load(savePath);std::filesystem::remove(path);
        require(loaded,"live scene load failed");
        ++epoch;scene.reset(epoch);strikes=synth.worldAudioStats().strikes;publish(false);
        require(synth.worldAudioStats().strikes==strikes,"load replayed a historical footstep");
        game.update({},1.f/60,false);publish(true);
        require(synth.worldAudioStats().strikes==strikes,"first post-load movement replayed a contact");
        for(int frame=0;frame<120;++frame){game.update({},1.f/60,false);publish(true);}
        require(game.pedestrians.size()==84&&game.vehicles.size()==50,"live population changed unexpectedly");
        require(energy>0&&peak>0,"integrated world mix remained silent");
        require(scene.stats().duplicateIdentities==0,"live population published duplicate identities");
        std::cout<<"Live scene audio passed: people="<<game.pedestrians.size()<<"; vehicles="<<game.vehicles.size()
                 <<"; audioFrames="<<rendered<<"; maximumEngines="<<maximumEngines<<"; maximumFeet="<<maximumFeet
                 <<"; contacts="<<scene.stats().contacts<<"; playedStrikes="<<synth.worldAudioStats().strikes
                 <<"; peak="<<peak<<"; energy="<<energy<<"; fingerprint="<<fingerprint<<'\n';
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
