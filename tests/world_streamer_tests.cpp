#include "../src/world_streamer.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace mc;
namespace {
const Chunk& find(const World& world,int x,int z) {
    for(const auto& chunk:world.chunks)if(chunk.x==x&&chunk.z==z)return chunk;
    throw std::runtime_error("Expected resident chunk is missing");
}
bool vectorEqual(Vec3 a,Vec3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
void equalChunks(const Chunk& a,const Chunk& b) {
    assert(a.x==b.x&&a.z==b.z&&a.mesh.indices==b.mesh.indices&&a.mesh.vertices.size()==b.mesh.vertices.size());
    for(size_t i=0;i<a.mesh.vertices.size();++i) {
        const auto& av=a.mesh.vertices[i];const auto& bv=b.mesh.vertices[i];
        assert(vectorEqual(av.position,bv.position)&&vectorEqual(av.normal,bv.normal)&&vectorEqual(av.color,bv.color)&&av.material==bv.material);
    }
    assert(a.solids.size()==b.solids.size()&&a.lights.size()==b.lights.size());
    for(size_t i=0;i<a.solids.size();++i)assert(vectorEqual(a.solids[i].min,b.solids[i].min)&&vectorEqual(a.solids[i].max,b.solids[i].max));
    for(size_t i=0;i<a.lights.size();++i) {
        const auto& al=a.lights[i];const auto& bl=b.lights[i];
        assert(vectorEqual(al.position,bl.position)&&vectorEqual(al.color,bl.color)&&vectorEqual(al.direction,bl.direction));
        assert(al.radius==bl.radius&&al.intensity==bl.intensity&&al.cone==bl.cone);
    }
}
void bounds(const WorldStreamStats& stats) {
    assert(stats.queued<=WorldStreamer::MaxQueued&&stats.inFlight<=WorldStreamer::MaxWorkers);
    assert(stats.completed<=WorldStreamer::MaxCompleted&&stats.completedBytes<=WorldStreamer::MaxCompletedBytes);
    assert(stats.pendingChunks<=49&&stats.stagedChunks<=stats.pendingChunks);
    assert(stats.stagedBytes<=49*WorldStreamer::MaxChunkBytes);
}
template<class Predicate>void until(Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!predicate()) {
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Timed out waiting for bounded streaming work");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void publish(WorldStreamer& service,World& world,Vec3 target,uint64_t epoch) {
    const uint64_t revision=world.revision;std::string error;
    until([&] {
        assert(service.update(world,target,epoch,error)&&error.empty());bounds(service.stats());
        assert(world.chunks.size()==49&&world.collisionReady(target));
        return world.revision>revision;
    });
}
void requestProtocol() {
    World world;world.stream({8,0,8});const Vertex* retained=find(world,0,0).mesh.vertices.data();
    assert(world.collisionReady({256,0,256})&&world.collisionReady({-256,0,-256}));
    assert(!world.collisionReady({384,0,0})&&!world.collisionReady({-257,0,0}));
    auto initial=world.requestStream({128,0,0},17);assert(initial.size()==7);
    for(size_t i=0;i<3;++i)assert(world.installChunk(World::buildChunk(initial[i])));
    assert(world.stagedChunkCount()==3&&world.pendingChunkCount()==7&&!world.publishReady());
    assert(world.revision==1&&world.chunks.size()==49&&world.collisionReady({128,0,0}));
    auto repeated=world.requestStream({128,0,0},17);assert(repeated.size()==4);
    for(const auto& request:repeated) {
        bool stable=false;for(const auto& old:initial)if(old.x==request.x&&old.z==request.z){assert(old.ticket==request.ticket);stable=true;}
        assert(stable);
    }
    auto overlap=world.requestStream({128,0,128},17);assert(world.stagedChunkCount()==2&&overlap.size()==11);
    assert(!world.installChunk(World::buildChunk(initial.front()))); // The dropped northern key cannot reappear.
    for(const auto& request:overlap)assert(world.installChunk(World::buildChunk(request)));
    assert(world.publishReady()&&!world.publishReady());assert(world.revision==2&&world.chunks.size()==49);
    assert(find(world,0,0).mesh.vertices.data()==retained);
    World expected;expected.stream({128,0,128});
    for(const auto& chunk:world.chunks)equalChunks(chunk,find(expected,chunk.x,chunk.z));
    auto old=world.requestStream({256,0,128},17);assert(!old.empty());
    auto fresh=world.requestStream({256,0,128},18);assert(fresh.size()==old.size());
    assert(!world.installChunk(World::buildChunk(old.front())));
    auto malformed=World::buildChunk(fresh.front());malformed.chunk.x+=1;
    assert(!world.installChunk(std::move(malformed)));
    assert(world.installChunk(World::buildChunk(fresh.front())));
    assert(!world.installChunk(World::buildChunk(fresh.front())));
    world.stream({-512,0,-512});assert(!world.installChunk(World::buildChunk(fresh.back())));
    assert(!world.publishReady()&&world.pendingChunkCount()==0);
    assert(!world.collisionReady({2048,0,2048}));
    std::puts("Request protocol: stable overlap tickets, stale epochs, duplicate rejection, atomic 49-chunk publication");
}
void workerEquivalence() {
    World world;world.stream({8,0,8});WorldStreamer service;publish(service,world,{128,0,0},31);
    World expected;expected.stream({128,0,0});for(const auto& chunk:world.chunks)equalChunks(chunk,find(expected,chunk.x,chunk.z));
    const auto stats=service.stats();assert(stats.publications==1&&stats.synchronousFallbacks==0&&stats.installed==7&&stats.scheduled==7);
    assert(stats.pendingChunks==0&&stats.stagedBytes==0);bounds(stats);
    std::printf("Worker publication: %llu scheduled, %llu installed, no synchronous fallback\n",static_cast<unsigned long long>(stats.scheduled),static_cast<unsigned long long>(stats.installed));
}
std::mutex gateMutex;
std::condition_variable gateChanged;
bool gateOpen=false;
uint64_t heldEpoch=0;
ChunkBuildResult gatedBuild(const ChunkBuildRequest& request) {
    {std::unique_lock<std::mutex> lock(gateMutex);if(request.epoch==heldEpoch)gateChanged.wait(lock,[]{return gateOpen;});}
    return World::buildChunk(request);
}
void setGate(uint64_t epoch,bool open) {
    {std::lock_guard<std::mutex> lock(gateMutex);heldEpoch=epoch;gateOpen=open;}gateChanged.notify_all();
}
void resetAndTeleport() {
    World world;world.stream({8,0,8});setGate(41,false);WorldStreamer service(2,gatedBuild);std::string error;
    assert(service.update(world,{128,0,0},41,error));
    until([&]{return service.stats().inFlight==2;});
    // The application may replace the World while old builders are blocked; they own no live references.
    World replacement;replacement.stream({4096,0,0});world=std::move(replacement);
    service.reset(42);assert(service.update(world,{4224,0,0},42,error));
    assert(world.chunks.size()==49&&world.collisionReady({4224,0,0}));
    setGate(41,true);publish(service,world,{4224,0,0},42);
    assert(service.stats().rejectedResults>=2&&service.stats().cancelledRequests>0);
    World expected;expected.stream({4224,0,0});for(const auto& chunk:world.chunks)equalChunks(chunk,find(expected,chunk.x,chunk.z));
    const auto before=service.stats();assert(service.update(world,{-4096,0,2048},42,error));
    assert(world.chunks.size()==49&&world.collisionReady({-4096,0,2048}));
    assert(service.stats().synchronousFallbacks==before.synchronousFallbacks+1);
    assert(service.stats().publications==before.publications+1);bounds(service.stats());
    std::puts("Reset and teleport: blocked old workers rejected; synchronous collision recovery immediately complete");
}
void fullQueueCancellation() {
    World world;world.stream({8,0,8});std::string error;
    {
        WorldStreamer service;assert(service.update(world,{256,0,256},52,error));
        until([&]{const auto stats=service.stats();bounds(stats);return stats.completed==4&&stats.inFlight>0;});
        service.reset(53);
        until([&]{const auto stats=service.stats();bounds(stats);return stats.inFlight==0;});
        const auto stats=service.stats();assert(stats.completed==0&&stats.completedBytes==0&&stats.rejectedResults>=4);
    }
    {
        WorldStreamer service;assert(service.update(world,{256,0,256},54,error));
        until([&]{const auto stats=service.stats();bounds(stats);return stats.completed==4&&stats.inFlight>0;});
        // Destruction must wake producers blocked by the completed-result cap and join them.
    }
    assert(world.revision==1&&world.chunks.size()==49);
    std::puts("Backpressure: cancellation and destruction drain a full queue without publishing partial chunks");
}
ChunkBuildResult oversizedBuild(const ChunkBuildRequest& request) {
    ChunkBuildResult result;result.request=request;result.chunk.x=request.x;result.chunk.z=request.z;
    result.chunk.mesh.vertices.reserve(WorldStreamer::MaxChunkBytes/sizeof(Vertex)+1);return result;
}
ChunkBuildResult failingBuild(const ChunkBuildRequest&) {throw std::runtime_error("Intentional builder failure");}
ChunkBuildResult unknownFailureBuild(const ChunkBuildRequest&) {throw 7;}
ChunkBuildResult mismatchedBuild(const ChunkBuildRequest& request) {
    ChunkBuildResult result;result.request=request;result.chunk.x=request.x+1;result.chunk.z=request.z;return result;
}
void builderFailures() {
    for(const auto& test:{std::pair<WorldStreamer::BuildFunction,const char*>{oversizedBuild,"8 MiB"},{failingBuild,"Intentional"},{unknownFailureBuild,"unknown exception"},{mismatchedBuild,"mismatched"}}) {
        World world;world.stream({8,0,8});WorldStreamer service(1,test.first);std::string error;
        until([&]{const bool ok=service.update(world,{128,0,0},61,error);bounds(service.stats());return !ok;});
        assert(error.find(test.second)!=std::string::npos&&world.revision==1&&world.chunks.size()==49);
        assert(world.collisionReady({128,0,0}));assert(service.stats().completedBytes==0);
    }
    std::puts("Builder failures: oversized capacities, exceptions and mismatched identities report bounded errors");
}
}
int main() {
    requestProtocol();workerEquivalence();resetAndTeleport();fullQueueCancellation();builderFailures();
    std::puts("World streamer tests passed.");
}
