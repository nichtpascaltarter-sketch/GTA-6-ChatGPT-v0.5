#pragma once
#include "game.h"
#include "renderer.h"
#include "world_streamer.h"
#include <array>
#include <chrono>
#include <ostream>
#include <utility>

namespace mc {
// Native diagnostic route. Each transition waits for CPU publication and GPU
// retirement, so slow software rendering cannot conceal an incomplete phase.
class StreamingProbe {
public:
    void beginFrame(Game& game,WorldStreamer& streamer,uint64_t& epoch,uint64_t frame) {
        if(started_||complete_)return;
        if(phase_==7){++epoch;streamer.reset(epoch);}
        const Vec2 point=route()[phase_];
        game.occupied=-1;game.player={point.x,game.world.height(point.x,point.y),point.y};
        game.yaw=.7f;game.pitch=.2f;game.messageTime=0;
        firstFrame_=frame;began_=Clock::now();started_=true;
    }
    bool observe(const World& world,const WorldStreamStats& cpu,const StreamStats& gpu,
                 uint64_t epoch,uint64_t frame,std::ostream& log,std::string& error) {
        if(complete_)return true;
        if(cpu.queued>WorldStreamer::MaxQueued||cpu.inFlight>WorldStreamer::MaxWorkers||
           cpu.completed>WorldStreamer::MaxCompleted||cpu.completedBytes>WorldStreamer::MaxCompletedBytes||
           cpu.pendingChunks>49||cpu.stagedChunks>49||cpu.stagedBytes>49*WorldStreamer::MaxChunkBytes) {
            error="Streaming diagnostic exceeded the bounded CPU queue or staging budget.";return false;
        }
        if(gpu.pendingBatches>4||gpu.pendingUploadBytes>384ull*1024*1024||
           gpu.vertexArenaBytes>256ull*1024*1024||gpu.indexArenaBytes>64ull*1024*1024) {
            error="Streaming diagnostic exceeded the bounded GPU residency budget.";return false;
        }
        if(std::chrono::duration<double>(Clock::now()-began_).count()>30) {
            error="Streaming diagnostic did not settle its current phase within 30 seconds.";return false;
        }
        const Vec2 point=route()[phase_];
        const int x=int(std::floor(point.x/World::ChunkSize)),z=int(std::floor(point.y/World::ChunkSize));
        if(frame<firstFrame_+4||cpu.pendingChunks||cpu.queued||cpu.inFlight||cpu.completed||gpu.pendingBatches)return true;
        if(world.chunks.size()!=49||gpu.residentChunks!=49||gpu.epoch!=epoch) {
            error="Streaming diagnostic settled with an incomplete CPU or GPU world.";return false;
        }
        for(int dz=-World::StreamRadius;dz<=World::StreamRadius;++dz)
            for(int dx=-World::StreamRadius;dx<=World::StreamRadius;++dx) {
                bool found=false;for(const auto& chunk:world.chunks)if(chunk.x==x+dx&&chunk.z==z+dz){found=true;break;}
                if(!found){error="Streaming diagnostic published the wrong chunk neighborhood.";return false;}
            }
        uint64_t expected=49;
        if(phase_>0&&previousEpoch_==epoch) {
            const int overlapX=std::max(0,7-std::abs(x-previousX_));
            const int overlapZ=std::max(0,7-std::abs(z-previousZ_));
            expected=49-uint64_t(overlapX*overlapZ);
        }
        const uint64_t uploaded=gpu.uploadedChunks-previousUploads_;
        const uint64_t retained=gpu.retainedChunks-previousRetained_;
        if(uploaded!=expected||retained!=49-expected||gpu.ordinaryWaits||gpu.pressureWaits||gpu.repackWaits) {
            error="Streaming diagnostic reuploaded retained chunks or stalled under its normal residency budget.";return false;
        }
        if(phase_>=1&&phase_<=3&&(cpu.synchronousFallbacks!=previousFallbacks_||
           cpu.scheduled-previousScheduled_!=expected||cpu.built-previousBuilt_!=expected||
           cpu.installed-previousInstalled_!=expected)) {
            error="Adjacent streaming diagnostic phases did not complete through the background workers.";return false;
        }
        log<<"Streaming phase "<<phase_<<": center="<<x<<','<<z<<"; epoch="<<epoch
           <<"; chunks="<<gpu.residentChunks<<"; uploaded="<<uploaded<<"; expected="<<expected
           <<"; retained="<<retained<<"; cpuPending="<<cpu.pendingChunks<<"; batches="<<gpu.pendingBatches
           <<"; fallbacks="<<cpu.synchronousFallbacks<<"; ordinaryWaits="<<gpu.ordinaryWaits
           <<"; pressureWaits="<<gpu.pressureWaits<<"; repacks="<<gpu.repackWaits
           <<"; residentBytes="<<gpu.residentBytes<<"; retiredBytes="<<gpu.retiredBytes
           <<"; scheduled="<<cpu.scheduled-previousScheduled_<<"; built="<<cpu.built-previousBuilt_
           <<"; installed="<<cpu.installed-previousInstalled_<<'\n';log.flush();
        previousX_=x;previousZ_=z;previousEpoch_=epoch;previousUploads_=gpu.uploadedChunks;previousRetained_=gpu.retainedChunks;
        previousFallbacks_=cpu.synchronousFallbacks;previousScheduled_=cpu.scheduled;
        previousBuilt_=cpu.built;previousInstalled_=cpu.installed;
        ++phase_;started_=false;complete_=phase_==route().size();
        if(complete_){log<<"Streaming verified: 8 settled phases; incremental uploads and epoch reset passed\n";log.flush();}
        return true;
    }
    bool complete() const {return complete_;}
private:
    using Clock=std::chrono::steady_clock;
    static const std::array<Vec2,8>& route() {
        static const std::array<Vec2,8> points{{{8,8},{136,8},{264,8},{392,136},{8,8},{2510,260},{8,8},{8,8}}};
        return points;
    }
    size_t phase_=0;bool started_=false,complete_=false;
    int previousX_=0,previousZ_=0;
    uint64_t firstFrame_=0,previousEpoch_=0,previousUploads_=0,previousRetained_=0;
    uint64_t previousFallbacks_=0,previousScheduled_=0,previousBuilt_=0,previousInstalled_=0;
    Clock::time_point began_{};
};
}
