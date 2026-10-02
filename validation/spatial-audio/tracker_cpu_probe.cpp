#include "../../src/audio_scene.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

// Measures only the production tracker, inside a naturally advancing full game.
// This deliberately excludes synthesis, rendering and asynchronous streaming.
int main() {
    try {
        for(float hour:{8.f,18.f}) {
            mc::Game game;game.initialize();game.dayTime=hour;
            mc::WorldAudioScene scene;scene.reset(1);
            std::vector<double> samples;samples.reserve(3600);
            unsigned maximumEngines=0,maximumFeet=0;
            for(unsigned frame=0;frame<3720;++frame) {
                game.update({},1.f/60,false);
                const auto eye=game.cameraEye(),forward=game.cameraTarget()-eye;
                const auto before=std::chrono::steady_clock::now();
                const auto state=scene.update(game,eye,forward,1.f/60,true,1);
                const auto after=std::chrono::steady_clock::now();
                if(frame>=120)samples.push_back(std::chrono::duration<double,std::micro>(after-before).count());
                maximumEngines=std::max(maximumEngines,state.engineCount);
                maximumFeet=std::max(maximumFeet,state.footCount);
            }
            const auto stats=scene.stats();
            if(stats.snapshots!=3720||stats.rejectedSamples||stats.duplicateIdentities||stats.capacityDrops||
               !stats.contacts||game.pedestrians.size()!=84||game.vehicles.size()!=50)
                throw std::runtime_error("tracker trace failed its population or source invariants");
            std::sort(samples.begin(),samples.end());
            double sum=0;for(double sample:samples)sum+=sample;
            std::printf("Tracker CPU: hour=%.0f; samples=%zu; warmup=120; meanUs=%.3f; medianUs=%.3f; p95Us=%.3f; maxUs=%.3f; contacts=%llu; engines=%u; feet=%u; storageBytes=%zu\n",
                double(hour),samples.size(),sum/double(samples.size()),samples[samples.size()/2],
                samples[samples.size()*95/100],samples.back(),static_cast<unsigned long long>(stats.contacts),
                maximumEngines,maximumFeet,sizeof(scene));
        }
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
