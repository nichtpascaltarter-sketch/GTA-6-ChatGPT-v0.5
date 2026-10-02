#include "../src/world_streamer.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using namespace mc;
namespace {
using Cell=std::pair<int,int>;

// Small, valid meshes isolate cache ownership and publication from generation cost.
ChunkBuildResult smallBuild(const ChunkBuildRequest& request,size_t capacityBytes=0) {
    ChunkBuildResult result;result.request=request;
    Chunk& chunk=result.chunk;chunk.x=request.x;chunk.z=request.z;
    const float x=request.x*World::ChunkSize,z=request.z*World::ChunkSize;
    if(capacityBytes>64)chunk.mesh.vertices.reserve((capacityBytes-64)/sizeof(Vertex));
    addQuad(chunk.mesh,{x,0,z},{x,0,z+World::ChunkSize},
        {x+World::ChunkSize,0,z+World::ChunkSize},{x+World::ChunkSize,0,z},
        {float(request.epoch),.4f,.6f});
    chunk.bounds={{x,0,z},{x+World::ChunkSize,.1f,z+World::ChunkSize}};
    return result;
}

void publishSmall(World& world,Vec3 position,uint64_t epoch,bool largeDeparting=false) {
    const auto requests=world.requestStream(position,epoch);
    assert(!requests.empty());
    for(const auto& request:requests) {
        assert(request.lod==WorldLod::Detail);
        const size_t bytes=largeDeparting&&request.x< -1?5u*1024u*1024u:0;
        assert(world.installChunk(smallBuild(request,bytes)));
    }
    assert(world.publishReady());
    assert(world.chunks.size()==49&&world.collisionReady(position));
}

ChunkBuildRequest requestAt(const std::vector<ChunkBuildRequest>& requests,int x,int z,WorldLod lod) {
    for(const auto& request:requests)if(request.x==x&&request.z==z&&request.lod==lod)return request;
    throw std::runtime_error("Expected distant request is missing");
}

RenderTileView tileAt(const World& world,int x,int z) {
    for(const auto& tile:world.renderTiles())if(tile.key.x==x&&tile.key.z==z)return tile;
    throw std::runtime_error("Expected render coverage is missing");
}

std::set<Cell> uniqueCoverage(const World& world) {
    std::set<Cell> cells;
    for(const auto& tile:world.renderTiles()) {
        assert(tile.mesh&&!tile.mesh->vertices.empty()&&!tile.mesh->indices.empty());
        assert(cells.insert({tile.key.x,tile.key.z}).second);
        if(tile.key.lod==WorldLod::Far)assert(!tile.rayTrace&&!tile.shadowCaster);
    }
    return cells;
}

float missingCellDistance(const World& world,Vec3 position,int centerX=0,int centerZ=0) {
    const auto cells=uniqueCoverage(world);
    float nearest=std::numeric_limits<float>::infinity();
    // Include one completely absent row around the entire prefetch envelope.
    const int radius=World::PrefetchRadius+1;
    for(int z=centerZ-radius;z<=centerZ+radius;++z)for(int x=centerX-radius;x<=centerX+radius;++x) {
        if(cells.count({x,z}))continue;
        const float lowX=x*World::ChunkSize,lowZ=z*World::ChunkSize;
        const float dx=std::max({lowX-position.x,0.0f,position.x-lowX-World::ChunkSize});
        const float dz=std::max({lowZ-position.z,0.0f,position.z-lowZ-World::ChunkSize});
        nearest=std::min(nearest,std::sqrt(dx*dx+dz*dz));
    }
    return nearest;
}

void defaultAndReadiness() {
    World world;publishSmall(world,{0,0,0},101);
    assert(!world.distantEnabled()&&world.requestDistant({0,0,0},101).empty());
    assert(uniqueCoverage(world).size()==49);
    for(const auto& tile:world.renderTiles())assert(tile.key.lod==WorldLod::Detail&&tile.rayTrace&&tile.shadowCaster);
    world.setDistantEnabled(true);
    const auto requests=world.requestDistant({0,0,0},101);assert(!requests.empty());
    assert(world.distantTileCount()==0&&world.detailFallbackCount()==0);
    const Vec3 offCenter{127,91,64};
    assert(std::abs(world.renderReadyRadius(offCenter)-385.0f)<.01f);
    assert(world.installDistant(smallBuild(requestAt(requests,4,0,WorldLod::Medium))));
    const float expected=missingCellDistance(world,offCenter);
    assert(expected>385&&expected<391);
    assert(std::abs(world.renderReadyRadius(offCenter)-expected)<.01f);
    assert(world.renderReadyRadius({4096,0,4096})==0);
    assert(world.renderReadyRadius({std::numeric_limits<float>::quiet_NaN(),0,0})==0);
    world.setDistantEnabled(false);
    assert(world.distantPendingCount()==0&&world.distantTileCount()==0&&world.distantBytes()==0);
    assert(world.detailFallbackCount()==0&&world.detailFallbackBytes()==0);
    assert(uniqueCoverage(world).size()==49&&world.chunks.size()==49);
    assert(!world.installDistant(smallBuild(requests.front())));
    std::puts("LOD default/readiness: unchanged detail49; exact off-center missing-cell distance; disable clears visuals");
}

void distantRequestProtocol() {
    World world;publishSmall(world,{0,0,0},201);world.setDistantEnabled(true);
    const auto initial=world.requestDistant({0,0,0},201);
    const auto medium=requestAt(initial,6,0,WorldLod::Medium);
    const auto far=requestAt(initial,12,0,WorldLod::Far);
    const auto moved=world.requestDistant({128,0,0},201);
    assert(requestAt(moved,6,0,WorldLod::Medium).ticket==medium.ticket);
    assert(requestAt(moved,12,0,WorldLod::Far).ticket==far.ticket);
    auto wrongTicket=medium;++wrongTicket.ticket;
    assert(!world.installDistant(smallBuild(wrongTicket)));
    auto wrongLod=medium;wrongLod.lod=WorldLod::Detail;
    assert(!world.installDistant(smallBuild(wrongLod)));
    auto malformed=smallBuild(far);++malformed.chunk.x;
    assert(!world.installDistant(std::move(malformed)));
    const auto fresh=world.requestDistant({128,0,0},202);
    assert(!world.installDistant(smallBuild(medium))&&!world.installDistant(smallBuild(far)));
    const auto newMedium=requestAt(fresh,6,0,WorldLod::Medium);
    const auto newFar=requestAt(fresh,12,0,WorldLod::Far);
    assert(newMedium.ticket!=medium.ticket&&newFar.ticket!=far.ticket);
    assert(world.installDistant(smallBuild(requestAt(fresh,6,0,WorldLod::Far))));
    assert(tileAt(world,6,0).key.lod==WorldLod::Far);
    assert(world.installDistant(smallBuild(newMedium))&&world.installDistant(smallBuild(newFar)));
    assert(tileAt(world,6,0).key.lod==WorldLod::Medium);
    assert(!world.installDistant(smallBuild(newMedium))&&!world.installDistant(smallBuild(newFar)));
    assert(world.chunks.size()==49&&world.collisionReady({0,0,0}));
    assert(uniqueCoverage(world).size()==51);
    std::puts("LOD requests: overlap tickets stable; stale epochs, wrong LOD/ticket, malformed and duplicate results rejected");
}

void representationAndHandover() {
    World world;publishSmall(world,{0,0,0},301);world.setDistantEnabled(true);
    const auto coarse=world.requestDistant({0,0,0},301);
    assert(world.installDistant(smallBuild(requestAt(coarse,4,0,WorldLod::Medium))));
    assert(tileAt(world,4,0).key.lod==WorldLod::Medium);
    const auto pending=world.requestStream({128,0,0},301);
    assert(pending.size()==7);const uint64_t revision=world.revision;
    for(size_t i=0;i+1<pending.size();++i)assert(world.installChunk(smallBuild(pending[i])));
    assert(!world.publishReady()&&world.revision==revision&&world.chunks.size()==49);
    assert(tileAt(world,4,0).key.lod==WorldLod::Medium);
    assert(world.installChunk(smallBuild(pending.back()))&&world.publishReady());
    assert(tileAt(world,4,0).key.lod==WorldLod::Detail);
    const auto before=uniqueCoverage(world);
    for(int z=-3;z<=3;++z)for(int x=-3;x<=4;++x)assert(before.count({x,z}));
    assert(world.detailFallbackCount()==7&&world.detailFallbackBytes()>0);
    assert(tileAt(world,-3,0).key.lod==WorldLod::Detail);
    const auto next=world.requestDistant({128,0,0},301);
    assert(world.installDistant(smallBuild(requestAt(next,-3,0,WorldLod::Medium))));
    assert(tileAt(world,-3,0).key.lod==WorldLod::Medium);
    assert(world.detailFallbackCount()==6);
    const auto after=uniqueCoverage(world);
    for(const auto& cell:before)assert(after.count(cell));
    assert(world.chunks.size()==49&&world.collisionReady({128,0,0}));
    std::puts("LOD handover: detail publishes atomically, wins overlapping cells, and remains until coarse coverage arrives");
}

void fallbackMemoryBound() {
    World world;publishSmall(world,{0,0,0},401,true);world.setDistantEnabled(true);
    world.requestDistant({0,0,0},401);
    publishSmall(world,{256,0,0},401);
    assert(world.detailFallbackBytes()<=World::MaxDetailFallbackBytes);
    assert(world.forcedCoarseBuilds()>0&&world.distantBytes()<=World::MaxVisualCacheBytes);
    const auto cells=uniqueCoverage(world);
    for(int z=-3;z<=3;++z)for(int x=-3;x<=5;++x)assert(cells.count({x,z}));
    size_t actualFar=0;
    for(int z=-3;z<=3;++z)for(int x=-3;x< -1;++x) {
        const auto tile=tileAt(world,x,z);
        if(tile.key.lod==WorldLod::Far) {
            ++actualFar;assert(tile.mesh->vertices.size()>4&&tile.mesh->indices.size()>6);
            for(const auto& vertex:tile.mesh->vertices)
                assert(std::isfinite(vertex.position.x)&&std::isfinite(vertex.position.y)&&std::isfinite(vertex.position.z));
        } else assert(tile.key.lod==WorldLod::Detail);
    }
    assert(actualFar>0&&world.chunks.size()==49&&world.collisionReady({256,0,0}));
    std::puts("LOD fallback cap: oversized departing detail is replaced with generated Far geometry without coverage holes");
}

void saturatedCacheHandover() {
    World world;publishSmall(world,{0,0,0},451,true);world.setDistantEnabled(true);
    const auto requests=world.requestDistant({0,0,0},451);
    const size_t maximumTiles=World::MaxVisualCacheBytes/World::MaxVisualChunkBytes;
    assert(maximumTiles>=3&&World::MaxVisualCacheBytes%World::MaxVisualChunkBytes==0);
    auto installFullTile=[&](const ChunkBuildRequest& request) {
        auto result=smallBuild(request,World::MaxVisualChunkBytes);
        const size_t vertexBytes=result.chunk.mesh.vertices.capacity()*sizeof(Vertex);
        result.chunk.mesh.indices.reserve((World::MaxVisualChunkBytes-vertexBytes)/sizeof(uint32_t));
        assert(World::chunkBytes(result.chunk)==World::MaxVisualChunkBytes);
        assert(world.installDistant(std::move(result)));
    };
    // The first underlay becomes this departing cell's only coverage after publication.
    // The second remains underneath retained detail and is safe to evict for emergency Far geometry.
    installFullTile(requestAt(requests,-3,-3,WorldLod::Far));
    installFullTile(requestAt(requests,0,0,WorldLod::Far));
    size_t installed=2;
    for(const auto& request:requests) {
        if(installed==maximumTiles)break;
        if(request.lod!=WorldLod::Far||request.x<4||request.x>World::FarRadius||std::abs(request.z)>World::FarRadius)continue;
        installFullTile(request);++installed;
    }
    assert(installed==maximumTiles&&world.distantBytes()==World::MaxVisualCacheBytes);
    const auto before=uniqueCoverage(world);
    publishSmall(world,{256,0,0},451);
    const auto after=uniqueCoverage(world);
    for(const auto& cell:before)assert(after.count(cell));
    for(int z=-3;z<=3;++z)for(int x=-3;x<=5;++x)assert(after.count({x,z}));
    assert(tileAt(world,-3,-3).key.lod==WorldLod::Far);
    assert(world.forcedCoarseBuilds()>0&&world.distantBytes()<World::MaxVisualCacheBytes);
    assert(world.detailFallbackBytes()<=World::MaxDetailFallbackBytes);
    assert(world.chunks.size()==49&&world.collisionReady({256,0,0}));
    std::puts("LOD saturated handover: eviction preserves departing cells' sole coarse coverage after detail publication");
}

void visualCacheMemoryBound() {
    World world;publishSmall(world,{0,0,0},501);world.setDistantEnabled(true);
    const auto requests=world.requestDistant({0,0,0},501);
    std::set<Cell> installed;bool deferred=false;
    for(const auto& request:requests) {
        auto result=smallBuild(request,World::MaxVisualChunkBytes);
        assert(World::chunkBytes(result.chunk)<=World::MaxVisualChunkBytes);
        if(!world.installDistant(std::move(result))){deferred=true;break;}
        installed.insert({request.x,request.z});
        assert(world.distantBytes()<=World::MaxVisualCacheBytes);
    }
    assert(deferred&&world.visualBudgetDeferrals()>0&&!installed.empty());
    assert(world.distantBytes()<=World::MaxVisualCacheBytes);
    const auto cells=uniqueCoverage(world);for(const auto& cell:installed)assert(cells.count(cell));
    const size_t bytes=world.distantBytes(),count=world.distantTileCount();
    assert(world.requestDistant({0,0,0},501).empty());
    assert(world.distantBytes()==bytes&&world.distantTileCount()==count);
    assert(world.chunks.size()==49&&world.collisionReady({0,0,0}));
    assert(!world.requestDistant({4096,0,0},501).empty());
    assert(world.distantBytes()<bytes);
    World oversized;publishSmall(oversized,{0,0,0},502);oversized.setDistantEnabled(true);
    const auto oversizedRequests=oversized.requestDistant({0,0,0},502);
    auto oversizedResult=smallBuild(oversizedRequests.front(),World::MaxVisualChunkBytes+1024);
    assert(World::chunkBytes(oversizedResult.chunk)>World::MaxVisualChunkBytes);
    assert(!oversized.installDistant(std::move(oversizedResult)));
    assert(oversized.distantBytes()==0&&oversized.visualBudgetDeferrals()==1);
    std::puts("LOD cache cap: capacity-based accounting defers over-budget work and preserves installed coverage");
}

void playableEdgeBounds() {
    constexpr int edge=int(World::Extent/World::ChunkSize)+World::PrefetchRadius;
    static_assert(edge>51);
    for(const Cell& cell:{Cell{edge,edge},Cell{-edge,-edge}}) {
        const ChunkBuildRequest request{cell.first,cell.second,601,1,WorldLod::Far};
        const auto result=World::buildChunk(request);
        assert(result.chunk.x==cell.first&&result.chunk.z==cell.second);
        assert(!result.chunk.mesh.vertices.empty()&&!result.chunk.mesh.indices.empty());
        assert(result.chunk.solids.empty()&&result.chunk.lights.empty());
        assert(World::chunkBytes(result.chunk)<=World::MaxVisualChunkBytes);
    }
    bool rejected=false;
    try {World::buildChunk({edge+1,0,601,2,WorldLod::Far});}catch(const std::out_of_range&){rejected=true;}
    assert(rejected);
    World world;publishSmall(world,{World::Extent,0,World::Extent},601);world.setDistantEnabled(true);
    const auto requests=world.requestDistant({World::Extent,0,World::Extent},601);
    assert(!requests.empty());bool beyondOldBound=false;
    for(const auto& request:requests) {
        assert(request.x<=edge&&request.x>=-edge&&request.z<=edge&&request.z>=-edge);
        if(request.x>51||request.z>51)beyondOldBound=true;
    }
    assert(beyondOldBound&&world.collisionReady({World::Extent,0,World::Extent}));
    std::puts("LOD world edge: genuine Far chunks cover the prefetch envelope beyond old detail-only limits");
}

template<class Predicate>void until(Predicate predicate,std::chrono::seconds timeout=std::chrono::seconds(10)) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(!predicate()) {
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Timed out waiting for distant streaming work");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void workerBounds(const WorldStreamStats& stats) {
    assert(stats.queued<=WorldStreamer::MaxQueued&&stats.inFlight<=WorldStreamer::MaxWorkers);
    assert(stats.completed<=stats.completedLimit&&stats.completedBytes<=WorldStreamer::MaxCompletedBytes);
    assert(stats.pendingChunks<=49&&stats.stagedChunks<=stats.pendingChunks);
}

void realOriginWarmup() {
    constexpr Vec3 origin{8,0,8};
    World world;assert(world.stream(origin));const uint64_t detailRevision=world.revision;
    WorldStreamer service;service.setDistantEnabled(true);std::string error;
    until([&] {
        assert(service.update(world,origin,901,error)&&error.empty());
        const auto stats=service.stats();workerBounds(stats);
        assert(stats.completedLimit==WorldStreamer::MaxDistantCompleted);
        assert(stats.synchronousFallbacks==0&&stats.visualBudgetDeferrals==0);
        assert(world.chunks.size()==49&&world.collisionReady(origin)&&world.revision==detailRevision);
        return stats.distantPending==0&&stats.queued==0&&stats.inFlight==0&&stats.completed==0;
    },std::chrono::seconds(30));
    const auto stats=service.stats();
    assert(world.distantTileCount()==1450&&stats.distantTiles==1450&&stats.distantInstalled==1450);
    assert(world.distantBytes()<=World::MaxVisualCacheBytes&&world.visualBudgetDeferrals()==0);
    assert(world.detailFallbackCount()==0&&world.forcedCoarseBuilds()==0);
    assert(uniqueCoverage(world).size()==1089);
    size_t detail=0,medium=0,far=0;
    for(const auto& tile:world.renderTiles()) {
        switch(tile.key.lod) {
            case WorldLod::Detail:++detail;break;
            case WorldLod::Medium:++medium;break;
            case WorldLod::Far:++far;break;
        }
    }
    assert(detail==49&&medium==176&&far==864);
    assert(std::abs(world.renderReadyRadius(origin)-2056.0f)<.01f);
    std::printf("LOD real warmup: 1450 cached tiles, 49/176/864 selected LODs, 2056m ready, %llu cache bytes\n",
        static_cast<unsigned long long>(world.distantBytes()));
}

void mediumHysteresisSelection() {
    World world;constexpr uint64_t epoch=902;
    for(int center=0;center<3;++center){
        const Vec3 position{float(center)*World::ChunkSize+8,0,8};
        publishSmall(world,position,epoch);world.setDistantEnabled(true);
        for(const auto& request:world.requestDistant(position,epoch))assert(world.installDistant(smallBuild(request)));
        size_t detail=0,medium=0,far=0,shadows=0;
        for(const auto& tile:world.renderTiles()){
            const int distance=std::max(std::abs(tile.key.x-center),std::abs(tile.key.z));
            if(distance<=World::StreamRadius)assert(tile.key.lod==WorldLod::Detail);
            else if(distance<=World::MediumRadius)assert(tile.key.lod==WorldLod::Medium);
            else if(distance>World::MediumRadius+1)assert(tile.key.lod==WorldLod::Far);
            detail+=tile.key.lod==WorldLod::Detail;medium+=tile.key.lod==WorldLod::Medium;far+=tile.key.lod==WorldLod::Far;
            shadows+=tile.shadowCaster;
        }
        assert(detail==49&&medium==size_t(center?191:176)&&far==size_t(center?849:864));
        assert(shadows==detail+medium&&uniqueCoverage(world).size()==1089);
        assert(world.detailFallbackCount()==0&&world.distantPendingCount()==0);
        assert(std::abs(world.renderReadyRadius(position)-2056.0f)<.01f);
    }
    std::puts("LOD medium hysteresis: axial moves retain one 15-tile edge; 49/191/849 complete selection and 240 shadow candidates");
}

std::mutex gateMutex;
std::condition_variable gateChanged;
uint64_t heldEpoch=0;
bool gateOpen=true;
void setGate(uint64_t epoch,bool open) {
    {std::lock_guard<std::mutex> lock(gateMutex);heldEpoch=epoch;gateOpen=open;}
    gateChanged.notify_all();
}
struct GateRelease {
    ~GateRelease(){setGate(0,true);}
};
ChunkBuildResult gatedSmallBuild(const ChunkBuildRequest& request) {
    {std::unique_lock<std::mutex> lock(gateMutex);if(request.epoch==heldEpoch)gateChanged.wait(lock,[]{return gateOpen;});}
    return smallBuild(request);
}

void workerReplacementAndTeleport() {
    World world;publishSmall(world,{0,0,0},701);setGate(701,false);
    WorldStreamer service(2,gatedSmallBuild);GateRelease release;service.setDistantEnabled(true);
    std::string error;assert(service.update(world,{0,0,0},701,error)&&error.empty());
    until([&]{workerBounds(service.stats());return service.stats().inFlight==2;});
    World replacement;publishSmall(replacement,{4096,0,0},702);world=std::move(replacement);
    service.reset(702);assert(service.update(world,{4096,0,0},702,error)&&error.empty());
    setGate(701,true);
    until([&] {
        assert(service.update(world,{4096,0,0},702,error)&&error.empty());workerBounds(service.stats());
        return world.distantTileCount()>0&&service.stats().rejectedResults>=2;
    });
    assert(world.chunks.size()==49&&world.collisionReady({4096,0,0}));
    for(const auto& tile:world.renderTiles())if(tile.key.lod!=WorldLod::Detail)
        assert(tile.mesh->vertices.front().color.x==702.0f);
    const uint64_t fallbacks=service.stats().synchronousFallbacks;
    assert(service.update(world,{-4096,0,2048},702,error)&&error.empty());
    assert(world.chunks.size()==49&&world.collisionReady({-4096,0,2048}));
    assert(service.stats().synchronousFallbacks==fallbacks+1);workerBounds(service.stats());
    uniqueCoverage(world);
    std::puts("LOD workers: blocked old-world jobs cannot install after replacement; teleport restores collision49 immediately");
}

void workerDisableCancellation() {
    World world;publishSmall(world,{0,0,0},801);setGate(801,false);
    WorldStreamer service(2,gatedSmallBuild);GateRelease release;service.setDistantEnabled(true);
    std::string error;assert(service.update(world,{0,0,0},801,error));
    until([&]{return service.stats().inFlight==2;});
    service.setDistantEnabled(false);assert(service.update(world,{0,0,0},801,error));
    assert(!world.distantEnabled()&&world.distantTileCount()==0&&world.distantPendingCount()==0);
    setGate(801,true);
    until([&] {
        assert(service.update(world,{0,0,0},801,error)&&error.empty());workerBounds(service.stats());
        return service.stats().inFlight==0;
    });
    assert(service.stats().rejectedResults>=2&&service.stats().cancelledRequests>0);
    assert(uniqueCoverage(world).size()==49&&world.distantBytes()==0);
    std::puts("LOD cancellation: disabling during blocked coarse builds discards queued and in-flight visual work");
}
}

int main() {
    defaultAndReadiness();distantRequestProtocol();representationAndHandover();
    fallbackMemoryBound();saturatedCacheHandover();visualCacheMemoryBound();playableEdgeBounds();
    workerReplacementAndTeleport();workerDisableCancellation();realOriginWarmup();mediumHysteresisSelection();
    std::puts("World LOD streaming tests passed.");
}
