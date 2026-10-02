#include "world_streamer.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace mc {
namespace {
bool sameRequest(const ChunkBuildRequest& a,const ChunkBuildRequest& b) {
    return a.x==b.x&&a.z==b.z&&a.epoch==b.epoch&&a.ticket==b.ticket;
}
bool contains(const std::vector<ChunkBuildRequest>& values,const ChunkBuildRequest& value) {
    for(const auto& entry:values)if(sameRequest(entry,value))return true;
    return false;
}
}
struct WorldStreamer::Impl {
    struct Completion {ChunkBuildResult result;size_t bytes=0;std::array<char,256> error{};};
    mutable std::mutex mutex;
    std::condition_variable changed;
    bool stopping=false,hasEpoch=false;
    uint64_t epoch=0;
    std::vector<ChunkBuildRequest> wanted,active;
    std::deque<ChunkBuildRequest> queued;
    std::vector<Completion> completed;
    std::vector<std::thread> workers;
    WorldStreamStats counters;
    BuildFunction build;

    static void setError(Completion& completion,const char* text) noexcept {
        size_t index=0;
        while(index+1<completion.error.size()&&text[index]) {completion.error[index]=text[index];++index;}
        completion.error[index]='\0';
    }

    explicit Impl(unsigned count,BuildFunction builder):build(builder) {
        if(!build)throw std::invalid_argument("World streamer requires a chunk builder");
        count=std::max(1u,std::min(unsigned(WorldStreamer::MaxWorkers),count));
        active.reserve(count);workers.reserve(count);wanted.reserve(49);completed.reserve(WorldStreamer::MaxCompleted);
        try {for(unsigned i=0;i<count;++i)workers.emplace_back([this]{run();});}
        catch(...) {
            {std::lock_guard<std::mutex> lock(mutex);stopping=true;}changed.notify_all();
            for(auto& worker:workers)if(worker.joinable())worker.join();
            throw;
        }
    }
    ~Impl() {
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;queued.clear();wanted.clear();}
        changed.notify_all();for(auto& worker:workers)if(worker.joinable())worker.join();
    }
    void run() {
        for(;;) {
            ChunkBuildRequest request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[&]{return stopping||!queued.empty();});
                if(stopping)return;
                request=queued.front();queued.pop_front();active.push_back(request);
            }
            Completion completion;completion.result.request=request;
            try {
                completion.result=build(request);
                if(!sameRequest(completion.result.request,request)||completion.result.chunk.x!=request.x||completion.result.chunk.z!=request.z)
                    throw std::runtime_error("Chunk builder returned a mismatched request");
                completion.bytes=World::chunkBytes(completion.result.chunk);
                if(completion.bytes>WorldStreamer::MaxChunkBytes) {
                    completion.result.chunk=Chunk{};completion.bytes=0;
                    setError(completion,"Generated chunk exceeds the 8 MiB streaming payload limit");
                }
            } catch(const std::exception& exception) {
                completion.result.request=request;completion.result.chunk=Chunk{};completion.bytes=0;setError(completion,exception.what());
            } catch(...) {
                completion.result.request=request;completion.result.chunk=Chunk{};completion.bytes=0;setError(completion,"Chunk generation failed with an unknown exception");
            }
            {
                std::unique_lock<std::mutex> lock(mutex);++counters.built;
                changed.wait(lock,[&]{return stopping||!contains(wanted,request)||
                    (completed.size()<WorldStreamer::MaxCompleted&&counters.completedBytes+completion.bytes<=WorldStreamer::MaxCompletedBytes);});
                active.erase(std::remove_if(active.begin(),active.end(),[&](const ChunkBuildRequest& entry){return sameRequest(entry,request);}),active.end());
                if(stopping)return;
                if(!contains(wanted,request)){++counters.rejectedResults;continue;}
                counters.completedBytes+=completion.bytes;completed.push_back(std::move(completion));
            }
        }
    }
    void reconcile(const std::vector<ChunkBuildRequest>& requests,bool schedule) {
        wanted=requests;
        for(auto iterator=queued.begin();iterator!=queued.end();) {
            if(!contains(wanted,*iterator)){iterator=queued.erase(iterator);++counters.cancelledRequests;}else ++iterator;
        }
        for(auto iterator=completed.begin();iterator!=completed.end();) {
            if(!contains(wanted,iterator->result.request)) {
                counters.completedBytes-=iterator->bytes;iterator=completed.erase(iterator);++counters.rejectedResults;
            } else ++iterator;
        }
        if(!schedule)return;
        for(const auto& request:wanted) {
            if(queued.size()>=WorldStreamer::MaxQueued)break;
            bool outstanding=contains(active,request);
            if(!outstanding)for(const auto& entry:queued)if(sameRequest(entry,request)){outstanding=true;break;}
            if(!outstanding)for(const auto& entry:completed)if(sameRequest(entry.result.request,request)){outstanding=true;break;}
            if(!outstanding){queued.push_back(request);++counters.scheduled;}
        }
    }
};

WorldStreamer::WorldStreamer(unsigned workers,BuildFunction build):impl(new Impl(workers,build)){}
WorldStreamer::~WorldStreamer()=default;
void WorldStreamer::reset(uint64_t epoch) {
    {
        std::lock_guard<std::mutex> lock(impl->mutex);impl->epoch=epoch;impl->hasEpoch=true;
        impl->counters.cancelledRequests+=impl->queued.size();impl->counters.rejectedResults+=impl->completed.size();
        impl->queued.clear();impl->completed.clear();impl->wanted.clear();impl->counters.completedBytes=0;
        impl->counters.pendingChunks=impl->counters.stagedChunks=impl->counters.stagedBytes=0;
    }
    impl->changed.notify_all();
}
bool WorldStreamer::update(World& world,Vec3 position,uint64_t epoch,std::string& error) {
    try {
        error.clear();
        if(!std::isfinite(position.x)||!std::isfinite(position.z)) {error="Streaming position is not finite";return false;}
        if(!impl->hasEpoch||impl->epoch!=epoch)reset(epoch);
        if(!world.collisionReady(position)) {
            reset(epoch);
            const bool published=world.stream(position);
            if(!world.collisionReady(position)){error="Synchronous streaming did not restore collision coverage";return false;}
            std::lock_guard<std::mutex> lock(impl->mutex);++impl->counters.synchronousFallbacks;
            if(published)++impl->counters.publications;
        }
        auto requests=world.requestStream(position,epoch);
        std::vector<Impl::Completion> ready;ready.reserve(MaxCompleted);
        {
            std::lock_guard<std::mutex> lock(impl->mutex);impl->reconcile(requests,false);
            ready.swap(impl->completed);impl->counters.completedBytes=0;
        }
        impl->changed.notify_all();
        for(auto& completion:ready) {
            if(completion.error[0]){error="World streaming: "+std::string(completion.error.data());reset(epoch);return false;}
            const bool installed=world.installChunk(std::move(completion.result));
            std::lock_guard<std::mutex> lock(impl->mutex);
            if(installed)++impl->counters.installed;else ++impl->counters.rejectedResults;
        }
        const bool published=world.publishReady();
        requests=world.requestStream(position,epoch);
        {
            std::lock_guard<std::mutex> lock(impl->mutex);impl->reconcile(requests,true);
            if(published)++impl->counters.publications;
            impl->counters.pendingChunks=world.pendingChunkCount();
            impl->counters.stagedChunks=world.stagedChunkCount();
            impl->counters.stagedBytes=world.stagedChunkBytes();
        }
        impl->changed.notify_all();return true;
    } catch(const std::exception& exception) {error="World streaming: "+std::string(exception.what());reset(epoch);return false;}
    catch(...) {error="World streaming failed with an unknown exception";reset(epoch);return false;}
}
WorldStreamStats WorldStreamer::stats() const {
    std::lock_guard<std::mutex> lock(impl->mutex);WorldStreamStats stats=impl->counters;
    stats.queued=impl->queued.size();stats.inFlight=impl->active.size();stats.completed=impl->completed.size();return stats;
}
}
