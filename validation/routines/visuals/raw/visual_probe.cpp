#include "game.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdint>
using namespace mc;
namespace {
void mix(uint64_t& hash,uint32_t value){for(int n=0;n<4;++n){hash^=uint8_t(value>>(n*8));hash*=1099511628211ull;}}
uint64_t fingerprint(const Mesh& mesh){uint64_t hash=1469598103934665603ull;for(const auto& v:mesh.vertices)for(float f:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.color.x,v.color.y,v.color.z,v.material})mix(hash,std::bit_cast<uint32_t>(f));for(uint32_t index:mesh.indices)mix(hash,index);return hash;}
void measure(const char* label,const Game& game){
    for(int i=0;i<7;++i)game.dynamicMesh();
    std::array<double,101> samples{};Mesh mesh;
    for(auto& elapsed:samples){const auto start=std::chrono::steady_clock::now();mesh=game.dynamicMesh();elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    std::sort(samples.begin(),samples.end());
    std::printf("%s vertices=%zu triangles=%zu bytes=%zu hash=%016llx median_ms=%.6f p95_ms=%.6f max_ms=%.6f\n",label,mesh.vertices.size(),mesh.indices.size()/3,mesh.vertices.size()*sizeof(Vertex)+mesh.indices.size()*sizeof(uint32_t),static_cast<unsigned long long>(fingerprint(mesh)),samples[50],samples[95],samples.back());
}
Game crowd(bool mixed,bool distant){
    Game game;game.player={0,0,distant?-60.f:0.f};game.time=2.5f;
    for(int i=0;i<100;++i){Pedestrian p;p.position={float(i%10)-4.5f,0,float(i/10)-4.5f};p.yaw=float(i)*.2f;p.phase=float(i)*.35f;
#ifdef MC_ROUTINE_VISUALS
        p.identity=uint32_t(i);p.motion=.6f;
        if(mixed){p.activity=PedestrianActivity(i%8);p.motion=(i%8==0||i%8==4)? .6f:i%8==7?1.f:0.f;p.activityTime=.3f;p.carrying=i%8==4;p.sitBlend=i%8==2?1.f:0.f;p.seatPosition=p.position+Vec3{0,.59f,0};}
#else
        (void)mixed;
#endif
        game.pedestrians.push_back(p);
    }return game;
}
}
int main(){
    measure("crowd_walk_close",crowd(false,false));measure("crowd_walk_far",crowd(false,true));
#ifdef MC_ROUTINE_VISUALS
    measure("crowd_activities_close",crowd(true,false));measure("crowd_activities_far",crowd(true,true));
#endif
    Game game;game.time=2.5f;game.player={0,0,0};measure("player_only",game);
    game.player=World::garageSite().counter;measure("workshop_attendant",game);
    game.player=Game::harborSplitContact();measure("trial_marshal",game);
    game.player={0,0,0};game.activeMission=4;game.missionStage=2;game.vehicles.resize(1);game.occupied=0;
    for(auto kind:{VehicleKind::Car,VehicleKind::Motorcycle,VehicleKind::Boat,VehicleKind::Aircraft}){game.vehicles[0].kind=kind;game.vehicles[0].yaw=.7f;game.vehicles[0].pitch=.1f;game.vehicles[0].roll=.1f;measure(kind==VehicleKind::Car?"rescue_car":kind==VehicleKind::Motorcycle?"rescue_motorcycle":kind==VehicleKind::Boat?"rescue_boat":"rescue_aircraft",game);}
}
