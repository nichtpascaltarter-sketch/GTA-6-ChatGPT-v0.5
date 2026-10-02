#include "world.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace mc {
GarageSite World::garageSite() {
    return {"Harbor Motor Works",{{154,.16f,78},{184,7.76f,98}},
        {{171,.16f,83},{179,2.4f,94}},{{157,.16f,83},{162,2.2f,87}},
        {175,.056f,111},{175,.16f,88},{160,.16f,84.5f},{160,.16f,81},
        {175,.16f,98},{160,.16f,98},{175,0,120},.16f,Pi};
}
bool World::stream(Vec3 position) {
    if(!std::isfinite(position.x)||!std::isfinite(position.z))return false;
    pending.clear();hasRequest=false;
    int x=int(std::floor(clamp(position.x,-Extent,Extent)/ChunkSize));
    int z=int(std::floor(clamp(position.z,-Extent,Extent)/ChunkSize));
    if(x==centerX && z==centerZ&&chunks.size()==49)return false;
    const auto requests=requestStream(position,requestEpoch);
    for(const auto& request:requests)installChunk(buildChunk(request));
    return publishReady();
}
size_t World::chunkBytes(const Chunk& chunk) {
    return chunk.mesh.vertices.capacity()*sizeof(Vertex)+chunk.mesh.indices.capacity()*sizeof(uint32_t)+
        chunk.solids.capacity()*sizeof(Box)+(chunk.lights.capacity()+chunk.alwaysLights.capacity())*sizeof(Light);
}
size_t World::stagedChunkCount() const {
    size_t count=0;for(const auto& entry:pending)if(entry.ready)++count;return count;
}
size_t World::stagedChunkBytes() const {
    size_t bytes=0;for(const auto& entry:pending)if(entry.ready)bytes+=chunkBytes(entry.chunk);return bytes;
}
std::vector<ChunkBuildRequest> World::requestStream(Vec3 position,uint64_t epoch) {
    if(!std::isfinite(position.x)||!std::isfinite(position.z))throw std::invalid_argument("Streaming position is not finite");
    const int x=int(std::floor(clamp(position.x,-Extent,Extent)/ChunkSize));
    const int z=int(std::floor(clamp(position.z,-Extent,Extent)/ChunkSize));
    if(hasRequest&&requestedX==x&&requestedZ==z&&requestEpoch==epoch) {
        std::vector<ChunkBuildRequest> requests;requests.reserve(pending.size());
        for(const auto& entry:pending)if(!entry.ready)requests.push_back(entry.request);
        return requests;
    }
    if(epoch!=requestEpoch){pending.clear();requestEpoch=epoch;}
    std::vector<PendingChunk> next;next.reserve(49);
    std::vector<ChunkBuildRequest> requests;requests.reserve(49);
    for(int iz=z-StreamRadius;iz<=z+StreamRadius;++iz)for(int ix=x-StreamRadius;ix<=x+StreamRadius;++ix) {
        bool resident=false;for(const auto& chunk:chunks)if(chunk.x==ix&&chunk.z==iz){resident=true;break;}
        if(resident)continue;
        PendingChunk entry;bool found=false;
        for(auto& old:pending)if(old.request.x==ix&&old.request.z==iz) {entry=std::move(old);found=true;break;}
        if(!found) {
            if(nextTicket==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Streaming ticket space exhausted");
            entry.request={ix,iz,epoch,++nextTicket};
        }
        if(!entry.ready)requests.push_back(entry.request);
        next.push_back(std::move(entry));
    }
    pending=std::move(next);requestedX=x;requestedZ=z;hasRequest=true;
    return requests;
}
ChunkBuildResult World::buildChunk(const ChunkBuildRequest& request) {
    if(request.lod!=WorldLod::Detail&&request.lod!=WorldLod::Medium&&request.lod!=WorldLod::Far)
        throw std::out_of_range("Chunk request has an invalid detail level");
    const int limit=int(Extent/ChunkSize)+(request.lod==WorldLod::Detail?StreamRadius:PrefetchRadius);
    if(request.x< -limit||request.x>limit||request.z< -limit||request.z>limit)
        throw std::out_of_range("Chunk request is outside world streaming bounds");
    World generator;
    Chunk chunk=generator.generate(request.x,request.z,request.lod);
    if(!chunk.mesh.vertices.empty()) {
        chunk.bounds={chunk.mesh.vertices.front().position,chunk.mesh.vertices.front().position};
        for(const auto& vertex:chunk.mesh.vertices) {
            const Vec3 p=vertex.position;
            chunk.bounds.min={std::min(chunk.bounds.min.x,p.x),std::min(chunk.bounds.min.y,p.y),std::min(chunk.bounds.min.z,p.z)};
            chunk.bounds.max={std::max(chunk.bounds.max.x,p.x),std::max(chunk.bounds.max.y,p.y),std::max(chunk.bounds.max.z,p.z)};
        }
    }
    return {request,std::move(chunk)};
}
bool World::installChunk(ChunkBuildResult&& result) {
    if(result.request.lod!=WorldLod::Detail||!hasRequest||result.request.epoch!=requestEpoch||result.chunk.x!=result.request.x||result.chunk.z!=result.request.z)return false;
    for(auto& entry:pending)if(entry.request.x==result.request.x&&entry.request.z==result.request.z&&entry.request.ticket==result.request.ticket) {
        if(entry.ready)return false;
        entry.chunk=std::move(result.chunk);entry.ready=true;return true;
    }
    return false;
}
bool World::publishReady() {
    if(!hasRequest||(requestedX==centerX&&requestedZ==centerZ&&pending.empty()&&chunks.size()==49))return false;
    for(const auto& entry:pending)if(!entry.ready)return false;
    // Validate the complete neighborhood before moving a single live collision or mesh allocation.
    for(int z=requestedZ-StreamRadius;z<=requestedZ+StreamRadius;++z)for(int x=requestedX-StreamRadius;x<=requestedX+StreamRadius;++x) {
        bool present=false;
        for(const auto& chunk:chunks)if(chunk.x==x&&chunk.z==z){present=true;break;}
        if(!present)for(const auto& entry:pending)if(entry.ready&&entry.chunk.x==x&&entry.chunk.z==z){present=true;break;}
        if(!present)return false;
    }
    std::vector<Chunk> next;next.reserve(49);
    retainDepartingDetail(requestedX,requestedZ);
    for(int z=requestedZ-StreamRadius;z<=requestedZ+StreamRadius;++z)for(int x=requestedX-StreamRadius;x<=requestedX+StreamRadius;++x) {
        bool moved=false;
        for(auto& chunk:chunks)if(chunk.x==x&&chunk.z==z){next.push_back(std::move(chunk));moved=true;break;}
        if(!moved)for(auto& entry:pending)if(entry.chunk.x==x&&entry.chunk.z==z){next.push_back(std::move(entry.chunk));break;}
    }
    chunks=std::move(next);centerX=requestedX;centerZ=requestedZ;pending.clear();hasRequest=false;++revision;++renderRevision;
    visualPlanDirty=true;pruneVisualCache();return true;
}
bool World::collisionReady(Vec3 position) const {
    if(chunks.size()!=49||!std::isfinite(position.x)||!std::isfinite(position.z))return false;
    const int x=int(std::floor(clamp(position.x,-Extent,Extent)/ChunkSize));
    const int z=int(std::floor(clamp(position.z,-Extent,Extent)/ChunkSize));
    for(int iz=z-1;iz<=z+1;++iz)for(int ix=x-1;ix<=x+1;++ix) {
        bool present=false;for(const auto& chunk:chunks)if(chunk.x==ix&&chunk.z==iz){present=true;break;}
        if(!present)return false;
    }
    return true;
}
void World::setDistantEnabled(bool enabled) {
    if(distant==enabled)return;
    distant=enabled;visualHasTarget=false;visualBudgetFull=false;visualPlanDirty=true;
    visualPending.clear();visualRequests.clear();visualCache.clear();detailFallback.clear();visualBytes=fallbackBytes=0;
    ++renderRevision;
}
const Chunk* World::selectedTile(int x,int z,WorldLod& lod) const {
    for(const auto& chunk:chunks)if(chunk.x==x&&chunk.z==z){lod=WorldLod::Detail;return &chunk;}
    const auto fallback=detailFallback.find({x,z,WorldLod::Detail});
    if(fallback!=detailFallback.end()){lod=WorldLod::Detail;return &fallback->second;}
    for(WorldLod level:{WorldLod::Medium,WorldLod::Far}) {
        const auto found=visualCache.find({x,z,level});
        if(found!=visualCache.end()){lod=level;return &found->second;}
    }
    return nullptr;
}
void World::pruneVisualCache() {
    if(!distant||!visualHasTarget)return;
    bool changed=false,freed=false;
    auto resident=[&](int x,int z){for(const auto& chunk:chunks)if(chunk.x==x&&chunk.z==z)return true;return false;};
    for(auto it=detailFallback.begin();it!=detailFallback.end();) {
        const auto key=it->first;
        const int distance=std::max(std::abs(key.x-visualX),std::abs(key.z-visualZ));
        if(distance>FarRadius||resident(key.x,key.z)||visualCache.count({key.x,key.z,WorldLod::Medium})||visualCache.count({key.x,key.z,WorldLod::Far})) {
            fallbackBytes-=chunkBytes(it->second);it=detailFallback.erase(it);changed=true;
        } else ++it;
    }
    for(auto it=visualCache.begin();it!=visualCache.end();) {
        const auto key=it->first;
        const int distance=std::max(std::abs(key.x-visualX),std::abs(key.z-visualZ));
        const bool replaced=resident(key.x,key.z)||detailFallback.count({key.x,key.z,WorldLod::Detail})||visualCache.count({key.x,key.z,WorldLod::Far});
        const bool remove=distance>PrefetchRadius||(key.lod==WorldLod::Medium&&distance>MediumRadius+1&&(distance>FarRadius||replaced));
        if(remove){visualBytes-=chunkBytes(it->second);it=visualCache.erase(it);changed=freed=true;}else ++it;
    }
    for(auto it=visualPending.begin();it!=visualPending.end();) {
        const auto key=it->first;
        const int distance=std::max(std::abs(key.x-visualX),std::abs(key.z-visualZ));
        if(distance>(key.lod==WorldLod::Far?PrefetchRadius:MediumRadius)||visualCache.count(key))it=visualPending.erase(it);else ++it;
    }
    if(freed)visualBudgetFull=false;
    if(changed){++renderRevision;visualPlanDirty=true;}
}
std::vector<ChunkBuildRequest> World::requestDistant(Vec3 position,uint64_t epoch) {
    if(!distant)return {};
    if(!std::isfinite(position.x)||!std::isfinite(position.z))throw std::invalid_argument("Distant streaming position is not finite");
    const int x=int(std::floor(clamp(position.x,-Extent,Extent)/ChunkSize));
    const int z=int(std::floor(clamp(position.z,-Extent,Extent)/ChunkSize));
    const bool moved=!visualHasTarget||x!=visualX||z!=visualZ;
    if(epoch!=visualEpoch){visualPending.clear();visualEpoch=epoch;visualPlanDirty=true;}
    visualX=x;visualZ=z;visualHasTarget=true;
    if(moved){visualBudgetFull=false;visualPlanDirty=true;++renderRevision;pruneVisualCache();}
    if(visualBudgetFull){visualRequests.clear();return {};}
    if(!visualPlanDirty)return visualRequests;
    auto ensure=[&](int cx,int cz,WorldLod lod) {
        RenderTileKey key{cx,cz,lod};
        if(visualCache.count(key)||visualPending.count(key))return;
        if(nextTicket==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Streaming ticket space exhausted");
        visualPending.emplace(key,ChunkBuildRequest{cx,cz,epoch,++nextTicket,lod});
    };
    for(int dz=-PrefetchRadius;dz<=PrefetchRadius;++dz)for(int dx=-PrefetchRadius;dx<=PrefetchRadius;++dx)ensure(x+dx,z+dz,WorldLod::Far);
    for(int dz=-MediumRadius;dz<=MediumRadius;++dz)for(int dx=-MediumRadius;dx<=MediumRadius;++dx)ensure(x+dx,z+dz,WorldLod::Medium);
    struct Ranked {ChunkBuildRequest request;int priority,distance,squared;};
    std::vector<Ranked> ranked;ranked.reserve(visualPending.size());
    for(const auto& [key,request]:visualPending) {
        const int dx=key.x-x,dz=key.z-z,distance=std::max(std::abs(dx),std::abs(dz));
        WorldLod selected=WorldLod::Far;const bool covered=selectedTile(key.x,key.z,selected)!=nullptr;
        int priority=3;
        if(key.lod==WorldLod::Medium)priority=2;
        else if(detailFallback.count({key.x,key.z,WorldLod::Detail}))priority=0;
        else if(!covered&&distance<=FarRadius)priority=1;
        else if(distance>FarRadius)priority=4;
        ranked.push_back({request,priority,distance,dx*dx+dz*dz});
    }
    std::sort(ranked.begin(),ranked.end(),[](const Ranked& a,const Ranked& b){
        if(a.priority!=b.priority)return a.priority<b.priority;
        if(a.distance!=b.distance)return a.distance<b.distance;
        if(a.squared!=b.squared)return a.squared<b.squared;
        if(a.request.z!=b.request.z)return a.request.z<b.request.z;
        return a.request.x<b.request.x;
    });
    visualRequests.clear();visualRequests.reserve(ranked.size());
    for(const auto& entry:ranked)visualRequests.push_back(entry.request);
    visualPlanDirty=false;return visualRequests;
}
bool World::installDistant(ChunkBuildResult&& result) {
    const auto request=result.request;
    if(!distant||!visualHasTarget||request.epoch!=visualEpoch||request.lod==WorldLod::Detail||
       result.chunk.x!=request.x||result.chunk.z!=request.z)return false;
    const RenderTileKey key{request.x,request.z,request.lod};const auto pendingRequest=visualPending.find(key);
    if(pendingRequest==visualPending.end()||pendingRequest->second.ticket!=request.ticket)return false;
    const size_t bytes=chunkBytes(result.chunk);
    if(bytes>MaxVisualChunkBytes||visualBytes+bytes>MaxVisualCacheBytes) {
        visualBudgetFull=true;++budgetDeferrals;return false;
    }
    const auto inserted=visualCache.emplace(key,std::move(result.chunk));
    if(!inserted.second)return false;
    visualBytes+=bytes;visualPending.erase(pendingRequest);++renderRevision;visualPlanDirty=true;pruneVisualCache();return true;
}
void World::retainDepartingDetail(int nextX,int nextZ) {
    if(!distant||!visualHasTarget)return;
    std::vector<Chunk*> outgoing;
    for(auto& chunk:chunks) {
        if(std::abs(chunk.x-nextX)<=StreamRadius&&std::abs(chunk.z-nextZ)<=StreamRadius)continue;
        if(std::abs(chunk.x-visualX)>FarRadius||std::abs(chunk.z-visualZ)>FarRadius)continue;
        if(visualCache.count({chunk.x,chunk.z,WorldLod::Medium})||visualCache.count({chunk.x,chunk.z,WorldLod::Far})||detailFallback.count({chunk.x,chunk.z,WorldLod::Detail}))continue;
        outgoing.push_back(&chunk);
    }
    auto meshBytes=[](const Chunk& chunk){return chunk.mesh.vertices.capacity()*sizeof(Vertex)+chunk.mesh.indices.capacity()*sizeof(uint32_t);};
    size_t needed=0;for(const Chunk* chunk:outgoing)needed+=meshBytes(*chunk);
    // If workers fall behind, build a cheap replacement before releasing any detailed coverage.
    for(auto it=outgoing.begin();fallbackBytes+needed>MaxDetailFallbackBytes&&it!=outgoing.end();) {
        Chunk* chunk=*it;
        auto result=buildChunk({chunk->x,chunk->z,visualEpoch,0,WorldLod::Far});
        const size_t bytes=chunkBytes(result.chunk);
        if(bytes>MaxVisualChunkBytes)throw std::runtime_error("Coarse fallback exceeds the visual chunk budget");
        if(visualBytes+bytes>MaxVisualCacheBytes) {
            // Coarse tiles covered by a better resident representation can be evicted without holes.
            for(auto cached=visualCache.begin();cached!=visualCache.end()&&visualBytes+bytes>MaxVisualCacheBytes;) {
                const auto key=cached->first;
                bool retained=false;
                for(const auto& live:chunks)if(live.x==key.x&&live.z==key.z&&
                    std::abs(live.x-nextX)<=StreamRadius&&std::abs(live.z-nextZ)<=StreamRadius){retained=true;break;}
                const WorldLod other=key.lod==WorldLod::Far?WorldLod::Medium:WorldLod::Far;
                const bool covered=retained||detailFallback.count({key.x,key.z,WorldLod::Detail})||visualCache.count({key.x,key.z,other});
                const bool outside=std::abs(key.x-visualX)>FarRadius||std::abs(key.z-visualZ)>FarRadius;
                if(covered||outside){visualBytes-=chunkBytes(cached->second);cached=visualCache.erase(cached);++renderRevision;}else ++cached;
            }
        }
        if(visualBytes+bytes>MaxVisualCacheBytes)throw std::runtime_error("Visual cache cannot retain a collision-safe coarse fallback within its byte budget");
        visualCache.emplace(RenderTileKey{chunk->x,chunk->z,WorldLod::Far},std::move(result.chunk));visualBytes+=bytes;
        visualPending.erase({chunk->x,chunk->z,WorldLod::Far});++coarseFallbacks;++renderRevision;
        needed-=meshBytes(*chunk);it=outgoing.erase(it);
    }
    if(fallbackBytes+needed>MaxDetailFallbackBytes)throw std::runtime_error("Detailed fallback reserve exhausted");
    // Allocate every map node before moving live mesh data; failed allocation leaves collision publication intact.
    std::map<RenderTileKey,Chunk> prepared;
    for(const Chunk* chunk:outgoing)prepared.emplace(RenderTileKey{chunk->x,chunk->z,WorldLod::Detail},Chunk{});
    for(Chunk* chunk:outgoing) {
        Chunk& saved=prepared.find({chunk->x,chunk->z,WorldLod::Detail})->second;
        saved.x=chunk->x;saved.z=chunk->z;saved.bounds=chunk->bounds;saved.mesh=std::move(chunk->mesh);
    }
    detailFallback.merge(prepared);fallbackBytes+=needed;
}
std::vector<RenderTileView> World::renderTiles() const {
    std::vector<RenderTileView> result;
    if(!distant||!visualHasTarget) {
        result.reserve(chunks.size());for(const auto& chunk:chunks)result.push_back({{chunk.x,chunk.z,WorldLod::Detail},&chunk.mesh,chunk.bounds,true,true});
        return result;
    }
    result.reserve(static_cast<size_t>((FarRadius*2+1)*(FarRadius*2+1)));
    for(int z=visualZ-FarRadius;z<=visualZ+FarRadius;++z)for(int x=visualX-FarRadius;x<=visualX+FarRadius;++x) {
        WorldLod lod=WorldLod::Far;const Chunk* tile=selectedTile(x,z,lod);
        if(tile)result.push_back({{x,z,lod},&tile->mesh,tile->bounds,lod!=WorldLod::Far,lod!=WorldLod::Far});
    }
    return result;
}
float World::renderReadyRadius(Vec3 position) const {
    if(!std::isfinite(position.x)||!std::isfinite(position.z)||chunks.empty())return 0;
    const int x=distant&&visualHasTarget?visualX:centerX,z=distant&&visualHasTarget?visualZ:centerZ;
    const int radius=distant&&visualHasTarget?FarRadius:StreamRadius;
    const float x0=(x-radius)*ChunkSize,z0=(z-radius)*ChunkSize,x1=(x+radius+1)*ChunkSize,z1=(z+radius+1)*ChunkSize;
    float ready=std::max(0.0f,std::min(std::min(position.x-x0,x1-position.x),std::min(position.z-z0,z1-position.z)));
    if(ready==0)return 0;
    for(int cz=z-radius;cz<=z+radius;++cz)for(int cx=x-radius;cx<=x+radius;++cx) {
        WorldLod lod=WorldLod::Far;if(selectedTile(cx,cz,lod))continue;
        const float dx=std::max(std::max(cx*ChunkSize-position.x,0.0f),position.x-(cx+1)*ChunkSize);
        const float dz=std::max(std::max(cz*ChunkSize-position.z,0.0f),position.z-(cz+1)*ChunkSize);
        ready=std::min(ready,std::sqrt(dx*dx+dz*dz));
    }
    return ready;
}
bool World::blocked(Vec3 p,float radius) const {
    radius=std::max(0.0f,radius);
    for(const auto& c:chunks)for(const auto& b:c.solids) {
        if(p.y>=b.max.y || p.y+1.7f<=b.min.y)continue;
        float x=clamp(p.x,b.min.x,b.max.x),z=clamp(p.z,b.min.z,b.max.z);
        float dx=p.x-x,dz=p.z-z;
        if(dx*dx+dz*dz<radius*radius || (p.x>b.min.x && p.x<b.max.x && p.z>b.min.z && p.z<b.max.z))return true;
    }
    return false;
}
Vec3 World::move(Vec3 from,Vec3 delta,float radius) const {
    radius=std::max(0.0f,radius);
    Vec3 p=from,remaining{delta.x,0,delta.z};
    // Recover safely if a saved position begins inside newly streamed collision geometry.
    for(int pass=0;pass<4;++pass) {
        bool moved=false;
        for(const auto& c:chunks)for(const auto& b:c.solids) {
            if(p.y>=b.max.y || p.y+1.7f<=b.min.y)continue;
            float loX=b.min.x-radius,hiX=b.max.x+radius,loZ=b.min.z-radius,hiZ=b.max.z+radius;
            if(p.x>loX && p.x<hiX && p.z>loZ && p.z<hiZ) {
                float distances[]={p.x-loX,hiX-p.x,p.z-loZ,hiZ-p.z};
                int side=0;for(int i=1;i<4;++i)if(distances[i]<distances[side])side=i;
                if(side==0)p.x=loX-.003f;else if(side==1)p.x=hiX+.003f;
                else if(side==2)p.z=loZ-.003f;else p.z=hiZ+.003f;
                moved=true;
            }
        }
        if(!moved)break;
    }
    // Continuous slab intersection prevents a fast vehicle crossing a thin wall in one update.
    for(int pass=0;pass<5;++pass) {
        float first=1;Vec3 normal{};bool hit=false;
        for(const auto& c:chunks)for(const auto& b:c.solids) {
            if(std::min(from.y,from.y+delta.y)>=b.max.y || std::max(from.y,from.y+delta.y)+1.7f<=b.min.y)continue;
            float enter=-std::numeric_limits<float>::infinity(),leave=1;Vec3 n{};bool valid=true;
            for(int axis=0;axis<2;++axis) {
                float pos=axis?p.z:p.x,d=axis?remaining.z:remaining.x;
                float lo=(axis?b.min.z:b.min.x)-radius,hi=(axis?b.max.z:b.max.x)+radius;
                if(std::abs(d)<1e-8f) {if(pos<=lo || pos>=hi){valid=false;break;}continue;}
                float near=(lo-pos)/d,far=(hi-pos)/d;float sign=-1;
                if(near>far){std::swap(near,far);sign=1;}
                if(near>enter) {enter=near;n=axis?Vec3{0,0,sign}:Vec3{sign,0,0};}
                leave=std::min(leave,far);
                if(enter>leave){valid=false;break;}
            }
            if(valid && enter>=-1e-6f && enter<first && leave>0 && dot(n,n)>0) {first=std::max(0.0f,enter);normal=n;hit=true;}
        }
        if(!hit){p+=remaining;break;}
        float distance=length(remaining);
        float safe=std::max(0.0f,first-.003f/std::max(distance,.003f));
        p+=remaining*safe;remaining=remaining*(1-first);
        float into=dot(remaining,normal);if(into<0)remaining-=normal*into;
        if(dot(remaining,remaining)<1e-8f)break;
    }
    p.y=from.y+delta.y;
    p.x=clamp(p.x,-Extent+1,Extent-1);p.z=clamp(p.z,-Extent+1,Extent-1);return p;
}
Mesh World::combinedMesh() const {
    Mesh m;size_t v=0,i=0;for(const auto& c:chunks){v+=c.mesh.vertices.size();i+=c.mesh.indices.size();}
    m.vertices.reserve(v);m.indices.reserve(i);for(const auto& c:chunks)appendMesh(m,c.mesh);return m;
}
}
