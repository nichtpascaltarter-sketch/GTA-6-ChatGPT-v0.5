#pragma once
#include "world.h"
#include <memory>

namespace mc {
struct WorldStreamStats {
    size_t queued=0,inFlight=0,completed=0,completedBytes=0;
    size_t completedLimit=4;
    size_t pendingChunks=0,stagedChunks=0,stagedBytes=0;
    size_t distantPending=0,distantTiles=0,distantBytes=0,detailFallbackTiles=0,detailFallbackBytes=0;
    uint64_t scheduled=0,built=0,installed=0,rejectedResults=0,cancelledRequests=0;
    uint64_t synchronousFallbacks=0,publications=0;
    uint64_t distantScheduled=0,distantBuilt=0,distantInstalled=0,forcedCoarseBuilds=0,visualBudgetDeferrals=0;
};

// The application owns this service outside movable/resettable World and Game values.
// update/reset are main-thread operations; stats is safe to read from any thread.
class WorldStreamer {
public:
    using BuildFunction=ChunkBuildResult(*)(const ChunkBuildRequest&);
    static constexpr size_t MaxQueued=8,MaxCompleted=4,MaxWorkers=2;
    static constexpr size_t MaxDistantCompleted=16,MaxCompletionBatch=32;
    static constexpr size_t MaxCompletedBytes=32u*1024u*1024u,MaxChunkBytes=8u*1024u*1024u;
    explicit WorldStreamer(unsigned workers=2,BuildFunction build=&World::buildChunk);
    ~WorldStreamer();
    WorldStreamer(const WorldStreamer&)=delete;
    WorldStreamer& operator=(const WorldStreamer&)=delete;
    // Increment epoch whenever the application replaces/loads/resets the World.
    void reset(uint64_t epoch);
    void setDistantEnabled(bool enabled,size_t completedLimit=MaxDistantCompleted);
    // Returns success, not whether a publication occurred; use world.revision or stats.
    bool update(World& world,Vec3 position,uint64_t epoch,std::string& error);
    WorldStreamStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
