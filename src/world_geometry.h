#pragma once
#include "world.h"

namespace mc::worldGeometry {
// Resolved once before either emitter: height is the actual style-limited body height.
struct BuildingSpec {
    Vec3 position;
    float halfWidth=0,halfDepth=0,height=0;
    uint32_t seed=0;
    bool suburban=false;
    int style=0;
    Vec3 color;
};
enum class TreeKind { Palm, Broadleaf, Cypress, Mangrove };
struct TreeSpec {Vec3 position;float scale=1;uint32_t seed=0;TreeKind kind=TreeKind::Broadleaf;};
std::vector<TreeSpec> describeNaturalTrees(const World&,int x,int z);
struct BlockSpec {
    uint32_t seed=0;
    Biome biome=Biome::Ocean;
    bool urban=false,plaza=false,exchange=false;
    std::vector<BuildingSpec> buildings;
};
BuildingSpec describeBuilding(Vec3 position,float halfWidth,float halfDepth,float height,uint32_t seed,bool suburban);
BlockSpec describeBlock(const World&,int x,int z);
// Appends only terrain, with the same canonical eight-metre boundary at every LOD.
// Returns the detailed shoreline coverage decision for the chunk's water plane.
bool appendTerrain(Mesh&,const World&,int x,int z,WorldLod);
}
