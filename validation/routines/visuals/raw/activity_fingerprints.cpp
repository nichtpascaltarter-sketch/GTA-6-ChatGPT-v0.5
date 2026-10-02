#include "game.h"
#include <bit>
#include <cstdio>
using namespace mc;
void mix(uint64_t& hash,uint32_t value){for(int byte=0;byte<4;++byte){hash^=uint8_t(value>>(byte*8));hash*=1099511628211ull;}}
int main(){for(float distance:{10.f,60.f})for(int activity=0;activity<8;++activity){
    Game game;game.player={distance,0,0};game.pedestrians.resize(5);
    for(auto& p:game.pedestrians)p.position={1000,0,1000};
    auto& p=game.pedestrians.back();p.position={0,0,0};p.identity=173;p.activity=PedestrianActivity(activity);
    p.phase=.7f;p.activityTime=.3f;p.motion=activity==0||activity==4?.6f:activity==7?1.f:0.f;
    p.carrying=activity==4;p.sitBlend=activity==2?1.f:0.f;p.seatPosition={0,.59f,-1.05f};
    const Mesh mesh=game.dynamicMesh();uint64_t hash=1469598103934665603ull;
    for(const auto& v:mesh.vertices)for(float field:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.color.x,v.color.y,v.color.z,v.material})mix(hash,std::bit_cast<uint32_t>(field));
    for(auto index:mesh.indices)mix(hash,index);
    std::printf("distance=%.0f activity=%d vertices=%zu triangles=%zu hash=%016llx\n",distance,activity,mesh.vertices.size(),mesh.indices.size()/3,static_cast<unsigned long long>(hash));
}}
