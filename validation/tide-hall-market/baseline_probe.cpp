#include "world.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <array>
uint64_t h=1469598103934665603ull;
void value(uint64_t v){for(int i=0;i<8;++i){h^=(v>>(i*8))&255;h*=1099511628211ull;}}
void vector(mc::Vec3 p){value(uint64_t(std::llround(p.x*1000)));value(uint64_t(std::llround(p.y*1000)));value(uint64_t(std::llround(p.z*1000)));}
int main(){mc::World world;world.stream({8,0,8});const auto graph=world.pedestrianNetwork({8,0,8});
for(const auto& p:mc::World::pedestrianPlaces()){value(p.id);value(p.siteId);value(uint64_t(p.kind));vector(p.position);vector(p.approach);vector(p.seatPosition);value(uint64_t(std::llround(p.yaw*1000)));value(p.capacity);value(p.sheltered);}std::printf("places %llu\n",(unsigned long long)h);h=1469598103934665603ull;
auto nodes=graph.nodes;std::sort(nodes.begin(),nodes.end(),[](const auto&a,const auto&b){return a.id<b.id;});for(const auto& n:nodes){value(n.id);vector(n.position);value(uint64_t(n.kind));}
std::vector<std::array<uint32_t,3>> edges;for(const auto&e:graph.edges){uint32_t a=graph.nodes[e.from].id,b=graph.nodes[e.to].id;if(a>b)std::swap(a,b);edges.push_back({a,b,e.crossingId});}std::sort(edges.begin(),edges.end());for(const auto&e:edges)for(auto v:e)value(v);std::printf("graph %llu nodes %zu edges %zu\n",(unsigned long long)h,graph.nodes.size(),graph.edges.size());
for(auto lod:{mc::WorldLod::Detail,mc::WorldLod::Medium,mc::WorldLod::Far}){auto c=mc::World::buildChunk({0,0,0,0,lod}).chunk;std::printf("LOD %d: %zu tris %zu bytes lights%zu solids%zu\n",int(lod),c.mesh.indices.size()/3,mc::World::chunkBytes(c),c.lights.size(),c.solids.size());}std::printf("combined %zu\n",world.combinedMesh().indices.size()/3);}
