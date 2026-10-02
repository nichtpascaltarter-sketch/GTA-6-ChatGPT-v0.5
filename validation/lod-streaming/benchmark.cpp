#include "src/world_streamer.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <thread>
#include <vector>
#include <set>
using namespace mc;
using Clock=std::chrono::steady_clock;
double ms(Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();}
int main(int argc,char** argv){
    const size_t limit=argc>1?size_t(std::atoi(argv[1])):16;
    const bool paced=argc<3||std::atoi(argv[2])!=0;
    World world;world.stream({8,0,8});WorldStreamer worker;worker.setDistantEnabled(true,limit);
    std::string error;std::vector<double> frames;const auto started=Clock::now();auto next=started;
    size_t completedPeak=0,completedBytes=0,framesToHorizon=0;float ready=0;double horizonMs=0;
    std::set<RenderTileKey> previous;for(const auto& tile:world.renderTiles())previous.insert(tile.key);
    std::vector<size_t> arrivals;size_t totalArrivalBytes=0;
    for(size_t frame=0;frame<(paced?1800u:100000u);++frame){
        const auto start=Clock::now();
        for(int call=0;call<2;++call){
            auto stats=worker.stats();completedPeak=std::max(completedPeak,stats.completed);completedBytes=std::max(completedBytes,stats.completedBytes);
            if(!worker.update(world,{8,0,8},1,error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
        }
        frames.push_back(ms(Clock::now()-start));
        const auto stats=worker.stats();ready=world.renderReadyRadius({8,0,8});
        if(!framesToHorizon&&ready>=2000){framesToHorizon=frame+1;horizonMs=ms(Clock::now()-started);}
        std::set<RenderTileKey> selected;size_t arrival=0;
        for(const auto& tile:world.renderTiles()){
            selected.insert(tile.key);
            if(!previous.count(tile.key))arrival+=tile.mesh->vertices.size()*sizeof(Vertex)+tile.mesh->indices.size()*sizeof(uint32_t);
        }
        previous=std::move(selected);arrivals.push_back(arrival);totalArrivalBytes+=arrival;
        if(world.chunks.size()!=49||!world.collisionReady({8,0,8})||stats.completed>stats.completedLimit||stats.completedBytes>WorldStreamer::MaxCompletedBytes||world.distantBytes()>World::MaxVisualCacheBytes)return 2;
        if(!stats.distantPending&&!stats.queued&&!stats.inFlight&&!stats.completed){
            std::sort(frames.begin(),frames.end());
            const auto view=world.renderTiles();size_t detail=0,medium=0,far=0;
            for(const auto& tile:view){detail+=tile.key.lod==WorldLod::Detail;medium+=tile.key.lod==WorldLod::Medium;far+=tile.key.lod==WorldLod::Far;}
            const double elapsed=ms(Clock::now()-started);std::sort(arrivals.begin(),arrivals.end());
            const auto idleStart=Clock::now();for(int i=0;i<10000;++i){if(!worker.update(world,{8,0,8},1,error)||!worker.update(world,{8,0,8},1,error))return 3;}
            const double idle=ms(Clock::now()-idleStart);
            std::printf("limit=%zu frames=%zu horizonFrames=%zu elapsedMs=%.3f callbackMedianMs=%.3f callbackP95Ms=%.3f callbackMaxMs=%.3f idlePairUs=%.3f completedPeak=%zu completedBytes=%zu cacheBytes=%zu cacheTiles=%zu ready=%.1f selected=%zu/%zu/%zu fallback=%llu budgetDeferrals=%llu\n",limit,frames.size(),framesToHorizon,elapsed,frames[frames.size()/2],frames[(frames.size()*95+99)/100-1],frames.back(),idle/10,completedPeak,completedBytes,world.distantBytes(),world.distantTileCount(),ready,detail,medium,far,(unsigned long long)stats.synchronousFallbacks,(unsigned long long)stats.visualBudgetDeferrals);
            std::printf("paced=%d horizonMs=%.3f selectedArrivalP95Bytes=%zu selectedArrivalMaxBytes=%zu selectedArrivalTotalBytes=%zu\n",paced?1:0,horizonMs,arrivals[(arrivals.size()*95+99)/100-1],arrivals.back(),totalArrivalBytes);
            return detail==49&&medium==176&&far==864&&ready>=2000?0:4;
        }
        if(paced){next+=std::chrono::nanoseconds(16666667);std::this_thread::sleep_until(next);}else std::this_thread::yield();
    }
    std::fprintf(stderr,"timeout pending=%zu cache=%zu ready=%f\n",world.distantPendingCount(),world.distantBytes(),ready);return 5;
}
