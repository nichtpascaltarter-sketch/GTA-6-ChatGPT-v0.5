#pragma once
#include "game.h"
#include "renderer.h"
#include "world_streamer.h"
#include <array>
#include <chrono>
#include <ostream>
#include <set>

namespace mc {
// Native integration probe: advance only after the complete visual neighborhood
// is installed and its GPU uploads/retirements have drained.
class LodProbe {
public:
    void beginFrame(Game& game,WorldStreamer& streamer,uint64_t& epoch,uint64_t frame) {
        if(started_||complete_)return;
        if(phase_==4){++epoch;streamer.reset(epoch);}
        const auto point=route()[phase_];
        game.occupied=-1;game.player={point.x,game.world.height(point.x,point.z),point.z};
        game.yaw=phase_==2?0.0f:phase_>=3?1.4f:.7f;game.pitch=.18f;game.messageTime=0;
        firstFrame_=frame;began_=Clock::now();started_=true;
    }
    void camera(const Game& game,Vec3& eye,Vec3& target) const {
        const auto point=route()[std::min(phase_,route().size()-1)];
        eye=game.cameraEye()+Vec3{0,point.y,0};
        target=game.cameraTarget()+Vec3{0,point.y,0};
    }
    bool observe(const World& world,const WorldStreamStats& cpu,const StreamStats& gpu,
                 Vec3 eye,uint64_t epoch,uint64_t frame,std::ostream& log,std::string& error) {
        if(complete_)return true;
        if(cpu.queued>WorldStreamer::MaxQueued||cpu.inFlight>WorldStreamer::MaxWorkers||
           cpu.completed>cpu.completedLimit||cpu.completedLimit!=WorldStreamer::MaxDistantCompleted||
           cpu.completedBytes>WorldStreamer::MaxCompletedBytes||cpu.pendingChunks>49||
           cpu.stagedChunks>49||cpu.stagedBytes>49*WorldStreamer::MaxChunkBytes||
           cpu.distantBytes>World::MaxVisualCacheBytes||cpu.detailFallbackBytes>World::MaxDetailFallbackBytes) {
            error="Distant-world diagnostic exceeded a CPU queue or cache budget.";return false;
        }
        if(gpu.pendingBatches>4||gpu.pendingUploadBytes>384ull*1024*1024||
           gpu.vertexArenaBytes>256ull*1024*1024||gpu.indexArenaBytes>64ull*1024*1024||
           gpu.ordinaryWaits||gpu.pressureWaits||gpu.repackWaits) {
            error="Distant-world diagnostic exceeded normal GPU residency or required a streaming wait.";return false;
        }
        if(gpu.epoch!=epoch||gpu.renderRevision!=world.renderRevision) {
            error="Distant-world diagnostic rendered a stale epoch or visual revision.";return false;
        }
        const auto views=world.renderTiles();
        std::set<std::pair<int,int>> cells;std::vector<RenderTileKey> rayKeys;
        std::array<uint32_t,3> counts{};std::array<uint64_t,3> bytes{};
        for(const auto& view:views){
            if(!view.mesh||!cells.emplace(view.key.x,view.key.z).second){
                error="Distant-world diagnostic selected an empty or overlapping tile.";return false;
            }
            const size_t lod=static_cast<size_t>(view.key.lod);++counts[lod];
            bytes[lod]+=view.mesh->vertices.size()*sizeof(Vertex)+view.mesh->indices.size()*sizeof(uint32_t);
            if(view.rayTrace)rayKeys.push_back(view.key);
        }
        if(gpu.residentChunks!=views.size()||gpu.residentTilesByLod!=counts||gpu.residentBytesByLod!=bytes){
            error="Distant-world GPU residency does not match the selected CPU meshes.";return false;
        }
        if(previousEpoch_==epoch&&previousRayKeys_==rayKeys&&
           gpu.uploadedTilesByLod[2]>previousFarUploads_&&gpu.tlasBuilds!=previousTlasBuilds_) {
            error="Far-only visual arrivals rebuilt the unchanged ray scene.";return false;
        }
        previousEpoch_=epoch;previousRayKeys_=std::move(rayKeys);
        previousFarUploads_=gpu.uploadedTilesByLod[2];previousTlasBuilds_=gpu.tlasBuilds;
        const float ready=world.renderReadyRadius(eye);
        if(!std::isfinite(gpu.fogEnd)||gpu.fogEnd<0||gpu.fogEnd>2000.01f||
           gpu.fogEnd>std::max(0.0f,ready-16.0f)+.1f){
            error="Distant-world fog exposes unready coverage.";return false;
        }
        if(std::chrono::duration<double>(Clock::now()-began_).count()>100){
            error="Distant-world diagnostic phase did not settle within 100 seconds.";return false;
        }
        if(frame<firstFrame_+4||cpu.pendingChunks||cpu.distantPending||cpu.queued||cpu.inFlight||cpu.completed||
           gpu.pendingBatches||gpu.retiredBytes)return true;
        if(world.chunks.size()!=49||counts!=std::array<uint32_t,3>{49,176,864}||ready<2000||gpu.fogEnd<1980||
           gpu.mainDrawn+gpu.mainCulled!=1089||!gpu.mainDrawn||!gpu.mainCulled||
           gpu.shadowDrawn+gpu.shadowCulled!=225||!gpu.shadowDrawn||!gpu.shadowCulled){
            error="Distant-world diagnostic settled without full coverage or useful draw culling.";return false;
        }
        const uint64_t detailUploads=gpu.uploadedTilesByLod[0]-settledDetailUploads_;
        if((phase_==1&&detailUploads!=7)||(phase_==4&&detailUploads!=49)){
            error="Distant-world transition reuploaded retained detailed geometry or missed an epoch reset.";return false;
        }
        const auto point=route()[phase_];
        log<<"LOD phase "<<phase_<<": center="<<int(std::floor(point.x/World::ChunkSize))<<','<<int(std::floor(point.z/World::ChunkSize))
           <<"; epoch="<<epoch<<"; detail="<<counts[0]<<"; medium="<<counts[1]<<"; far="<<counts[2]
           <<"; ready="<<ready<<"; fogEnd="<<gpu.fogEnd<<"; cacheBytes="<<cpu.distantBytes<<"; fallbackBytes="<<cpu.detailFallbackBytes
           <<"; residentBytes="<<gpu.residentBytes<<"; uploaded="<<gpu.uploadedChunks
           <<"; uploadedDetail="<<gpu.uploadedTilesByLod[0]<<"; uploadedMedium="<<gpu.uploadedTilesByLod[1]<<"; uploadedFar="<<gpu.uploadedTilesByLod[2]
           <<"; tlasBuilds="<<gpu.tlasBuilds<<"; rayInstances="<<gpu.rayInstances
           <<"; mainDrawn="<<gpu.mainDrawn<<"; mainCulled="<<gpu.mainCulled<<"; shadowDrawn="<<gpu.shadowDrawn<<"; shadowCulled="<<gpu.shadowCulled
           <<"; batches="<<gpu.pendingBatches<<"; retiredBytes="<<gpu.retiredBytes<<"; ordinaryWaits="<<gpu.ordinaryWaits
           <<"; pressureWaits="<<gpu.pressureWaits<<"; repacks="<<gpu.repackWaits<<"; frame="<<frame<<'\n';log.flush();
        settledDetailUploads_=gpu.uploadedTilesByLod[0];++phase_;started_=false;complete_=phase_==route().size();
        if(complete_){log<<"LOD verified: 5 settled phases; coverage, residency, culling and epoch reset passed\n";log.flush();}
        return true;
    }
    bool complete() const {return complete_;}
private:
    using Clock=std::chrono::steady_clock;
    static const std::array<Vec3,5>& route(){
        static const std::array<Vec3,5> points{{{8,0,8},{136,0,8},{-3200,90,-610},{2510,0,260},{2510,0,260}}};
        return points;
    }
    size_t phase_=0;bool started_=false,complete_=false;
    uint64_t firstFrame_=0,previousEpoch_=0,previousFarUploads_=0,previousTlasBuilds_=0,settledDetailUploads_=0;
    std::vector<RenderTileKey> previousRayKeys_;
    Clock::time_point began_{};
};
}
