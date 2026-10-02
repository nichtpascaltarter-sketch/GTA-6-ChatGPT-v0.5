#include "../src/audio_scene.h"
#include "../src/world_synth.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
void require(bool value,const char* message) {
    if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::abort();}
}
struct TemporarySave {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("meridian_gunfire_audio_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sav");
    ~TemporarySave(){std::error_code error;std::filesystem::remove(path,error);auto temp=path;temp+=".tmp";std::filesystem::remove(temp,error);}
};
}
int main() {
    mc::Game game;game.player={0,game.world.height(0,0),0};game.health=80;game.world.stream(game.player);
    game.pedestrians.resize(4);for(auto& person:game.pedestrians)person.health=0;
    game.pedestrians[0].health=100;game.pedestrians[0].position={0,game.world.height(0,16),16};game.pedestrians[0].yaw=mc::Pi;
    game.wanted=1;
    mc::WorldAudioScene scene;mc::WorldSynth synth;std::array<float,1600> block{};
    const mc::Vec3 listener{0,3,0},forward{0,0,1};std::uint64_t epoch=1;
    unsigned emitted=0,peakVoices=0;bool skipped=false,heard=false;
    auto step=[&]() {
        game.update({},1.f/60,false);
        auto snapshot=scene.update(game,listener,forward,1.f/60,true,epoch);
        require(snapshot.gunCount==4,"real Game officer roster failed to publish every baseline");
        for(const auto& event:game.lawShots()) {
            const mc::WorldGunSound* matched=nullptr;
            for(unsigned i=0;i<snapshot.gunCount;++i)if(snapshot.gunfire[i].shooter==event.shooter)matched=&snapshot.gunfire[i];
            require(matched&&matched->shotSerial==event.sequence&&matched->strength==1&&matched->ageSeconds==0,
                    "actual Game emission did not become an immediate audio payload");
            require(matched->origin==std::array<float,3>{event.origin.x,event.origin.y,event.origin.z},
                    "audio origin differed from the actual emitted muzzle ray");
            require(matched->left>0&&matched->right>0,"nearby real muzzle received no spatial gain");
        }
        emitted+=unsigned(game.lawShots().size());
        // Model one coalesced-away mailbox publication on the first real shot.
        if(!skipped&&!game.lawShots().empty())skipped=true;
        else synth.update(snapshot);
        synth.render(block.data(),block.size()/2);
        for(float sample:block){require(std::isfinite(sample)&&std::abs(sample)<1,"live Game gunfire was unbounded");heard=heard||sample!=0;}
        peakVoices=std::max(peakVoices,synth.stats().gunVoices);
        require(game.shotFlash==0,"police gunfire leaked into the player's flash path");
    };
    for(unsigned frame=0;frame<240&&emitted<3;++frame)step();
    require(emitted>=3&&skipped&&heard&&peakVoices>0,"live scene never exercised real gunfire and retained delivery");
    require(synth.stats().gunshots==emitted,"real emissions were lost or duplicated by live tracker/DSP");
    const auto beforePause=synth.stats().gunshots;
    for(unsigned i=0;i<5;++i) {
        auto paused=scene.update(game,listener,forward,0,false,epoch);synth.update(paused,true);synth.render(block.data(),800);
    }
    require(synth.stats().gunVoices==0&&synth.stats().gunshots==beforePause,"focus-style suspension replayed or retained a voice");
    game.paused=true;game.update({},1.f/60,false);require(game.lawShots().empty(),"Game pause preserved per-frame gunfire");
    game.paused=false;
    TemporarySave save;require(game.save(save.path.string()),"could not save live gunfire state");
    require(game.load(save.path.string())&&game.lawShots().empty(),"load retained a transient police emission");
    scene.reset(++epoch);synth.update(scene.update(game,listener,forward,0,false,epoch));synth.render(block.data(),800);
    require(synth.stats().gunshots==beforePause,"loaded shot counter replayed as an emission");
    for(unsigned frame=0;frame<90;++frame)step();
    require(synth.stats().gunshots==emitted&&emitted>beforePause,"fresh police gunfire did not resume after save/load");
    std::printf("Live gunfire audio: actual_emissions=%u accepted=%llu peak_voices=%u; muzzle identity, missed publication, pause, player-flash separation and save/load passed.\n",
        emitted,static_cast<unsigned long long>(synth.stats().gunshots),peakVoices);
}
