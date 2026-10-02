#pragma once
#include "mc_math.h"
#include <vector>
#include <string>

namespace mc {
struct Vertex { Vec3 position; Vec3 normal; Vec3 color; float material=0; };
struct Mesh { std::vector<Vertex> vertices; std::vector<uint32_t> indices; void clear(){vertices.clear();indices.clear();} };
struct Box {Vec3 min,max;};
struct Light {Vec3 position;float radius=24;Vec3 color{1,.72f,.4f};float intensity=70;Vec3 direction{0,-1,0};float cone=-1;};
enum class Biome { Downtown, Residential, Countryside, Wetland, Beach, Island, Ocean };
struct Landmark { Vec3 position; const char* name; };
struct Chunk {int x=0,z=0; Mesh mesh; std::vector<Box> solids;std::vector<Light> lights;};
// Value-only jobs: workers never retain a World, Game, or resident-chunk reference.
struct ChunkBuildRequest {int x=0,z=0;uint64_t epoch=0,ticket=0;};
struct ChunkBuildResult {ChunkBuildRequest request;Chunk chunk;};
// Material: 0 matte, 1 metal, 2 window/emissive, 3 water, 4 road.
void addBox(Mesh&,Vec3 center,Vec3 half,Vec3 color,float yaw=0,float material=0);
void addCylinder(Mesh&,Vec3 bottom,float radius,float height,Vec3 color,int sides=8,float material=0);
void addQuad(Mesh&,Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 color,float material=0);
void appendMesh(Mesh&,const Mesh&);
class World {
public:
    static constexpr float WaterLevel=-1.8f;
    static constexpr float ChunkSize=128.0f;
    static constexpr float Extent=6144.0f;
    static constexpr int StreamRadius=3;
    std::vector<Chunk> chunks;
    uint64_t revision=0;
    bool stream(Vec3 position);
    std::vector<ChunkBuildRequest> requestStream(Vec3 position,uint64_t epoch);
    static ChunkBuildResult buildChunk(const ChunkBuildRequest& request);
    bool installChunk(ChunkBuildResult&& result);
    bool publishReady();
    bool collisionReady(Vec3 position) const;
    size_t pendingChunkCount() const {return pending.size();}
    size_t stagedChunkCount() const;
    size_t stagedChunkBytes() const;
    static size_t chunkBytes(const Chunk& chunk);
    float height(float x,float z) const;
    float waterDepth(float x,float z) const;
    Biome biome(float x,float z) const;
    bool road(float x,float z) const;
    bool blocked(Vec3 position,float radius) const;
    Vec3 move(Vec3 from,Vec3 delta,float radius) const;
    Mesh combinedMesh() const;
    const char* district(Vec3 position) const;
    static const std::vector<Landmark>& landmarks();
    // Position along the complete winding coastal road; fraction is in [0,1].
    static Vec3 coastalRoadPoint(float fraction);
private:
    struct PendingChunk {ChunkBuildRequest request;Chunk chunk;bool ready=false;};
    int centerX=0x7fffffff,centerZ=0x7fffffff;
    int requestedX=0x7fffffff,requestedZ=0x7fffffff;
    uint64_t requestEpoch=0,nextTicket=0;
    bool hasRequest=false;
    std::vector<PendingChunk> pending;
    Chunk generate(int x,int z) const;
};
}
