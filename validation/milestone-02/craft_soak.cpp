#include "game.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace mc;
constexpr float Step=1.0f/60.0f;
std::string root;
uint64_t ticks=0,meshes=0,roundTrips=0;
int failures=0;
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
void require(bool good,const std::string& message){if(!good)throw std::runtime_error(message);}
std::string vectorText(Vec3 p){char out[160];std::snprintf(out,sizeof(out),"(%.9g,%.9g,%.9g)",p.x,p.y,p.z);return out;}
void checkState(const Game& g){
    require(finite(g.player),"nonfinite player");
    require(std::abs(g.player.x)<=World::Extent&&std::abs(g.player.z)<=World::Extent,"player out of world bounds "+vectorText(g.player));
    require(std::isfinite(g.health)&&g.health>=0&&g.health<=100,"invalid health");
    require(g.ammo>=0&&g.ammo<=30&&g.reserveAmmo>=0&&g.reserveAmmo<=10000,"invalid ammunition");
    require(g.wanted>=0&&g.wanted<=5,"invalid wanted level");
    require(g.occupied>=-1&&g.occupied<int(g.vehicles.size()),"invalid occupied index");
    require(std::isfinite(g.time)&&std::isfinite(g.dayTime)&&g.dayTime>=0&&g.dayTime<24,"invalid clock");
    require(std::isfinite(g.rain)&&g.rain>=0&&g.rain<=1,"invalid weather");
    for(size_t i=0;i<g.vehicles.size();++i){const auto& v=g.vehicles[i];require(finite(v.position)&&finite(v.velocity)&&std::isfinite(v.yaw)&&std::isfinite(v.speed),"nonfinite vehicle "+std::to_string(i));require(v.health>=0&&v.health<=100,"invalid vehicle health");require(std::isfinite(v.pitch)&&std::isfinite(v.roll)&&std::isfinite(v.throttle)&&std::abs(v.pitch)<=Pi*.5f&&std::abs(v.roll)<=Pi&&v.throttle>=-1&&v.throttle<=1,"invalid craft attitude or throttle "+std::to_string(i));}
    for(size_t i=0;i<g.pedestrians.size();++i){const auto& p=g.pedestrians[i];require(finite(p.position)&&std::isfinite(p.yaw)&&std::isfinite(p.phase)&&std::isfinite(p.panic),"nonfinite pedestrian "+std::to_string(i));require(p.health>=0&&p.health<=100,"invalid pedestrian health");}
}
void checkCamera(const Game& g){Vec3 eye=g.cameraEye(),target=g.cameraTarget();require(finite(eye)&&finite(target),"nonfinite camera");require(length(target-eye)>.01f,"degenerate camera");}
void checkMesh(const Mesh& mesh,const char* label){
    require(mesh.indices.size()%3==0,std::string(label)+" incomplete triangle");
    for(const auto& v:mesh.vertices)require(finite(v.position)&&finite(v.normal)&&finite(v.color)&&std::isfinite(v.material),std::string(label)+" nonfinite vertex");
    for(uint32_t i:mesh.indices)require(i<mesh.vertices.size(),std::string(label)+" invalid index "+std::to_string(i)+" / "+std::to_string(mesh.vertices.size()));
    ++meshes;
}
std::vector<char> read(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
void roundTrip(Game& game,const std::string& name,int frame,bool restore){
    const auto path=root+"/"+name+"-"+std::to_string(frame)+".sav",second=path+".copy";
    require(game.save(path),"save returned false at "+path);
    Game loaded;
    if(!loaded.load(path)){
        std::cerr<<"LOAD_REJECTED "<<name<<" frame="<<frame<<" player="<<vectorText(game.player)<<" file="<<path<<'\n';
        for(size_t i=0;i<game.vehicles.size();++i){const auto& v=game.vehicles[i];if(std::abs(v.position.x)>World::Extent||std::abs(v.position.z)>World::Extent||v.position.y< -100||v.position.y>4096||std::abs(v.yaw)>Pi*2||std::abs(v.speed)>150||length(v.velocity)>200)std::cerr<<"suspect vehicle "<<i<<" pos="<<vectorText(v.position)<<" yaw="<<v.yaw<<" speed="<<v.speed<<'\n';}
        for(size_t i=0;i<game.pedestrians.size();++i){const auto& p=game.pedestrians[i];if(std::abs(p.position.x)>World::Extent||std::abs(p.position.z)>World::Extent||p.position.y< -100||p.position.y>4096||std::abs(p.yaw)>Pi*2)std::cerr<<"suspect pedestrian "<<i<<" pos="<<vectorText(p.position)<<" yaw="<<p.yaw<<'\n';}
        throw std::runtime_error("game rejected its own freshly written save");
    }
    require(loaded.save(second),"resave returned false");require(read(path)==read(second),"saved fields changed during round trip");
    checkState(loaded);checkCamera(loaded);++roundTrips;
    if(restore)require(game.load(path),"in-place load failed");
    std::filesystem::remove(path);std::filesystem::remove(second);
}
uint64_t boatMoving=0,occupiedAirborne=0,passiveAirborne=0,lightChecks=0;
float maximumAltitude=0;
int craftIndex(const Game& g,VehicleKind kind){for(size_t i=0;i<g.vehicles.size();++i)if(g.vehicles[i].kind==kind)return int(i);throw std::runtime_error("missing craft");}
float surface(const Game& g,Vec3 p){float ground=g.world.height(p.x,p.z);return g.world.waterDepth(p.x,p.z)>.5f?std::max(ground,World::WaterLevel):ground;}
void initialize(Game& g){g.initialize();require(g.vehicles.size()==50,"expected current 50-vehicle population");}
void placeCraft(Game& g,int index,Vec3 position,float heading,float speed,float throttle,bool occupied){
    auto& v=g.vehicles[size_t(index)];v.position=position;v.yaw=heading;v.speed=speed;v.velocity=forward(heading)*speed;v.health=100;v.pitch=v.roll=v.steer=0;v.throttle=throttle;v.parked=false;
    g.occupied=occupied?index:-1;g.player=occupied?position:Vec3{position.x-14,g.world.height(position.x-14,position.z),position.z};g.yaw=heading;g.world.stream(g.player);
}
void inspect(Game& g,int frame){
    checkState(g);if(frame%60==0)checkCamera(g);
    for(size_t i=0;i<g.vehicles.size();++i){const auto& v=g.vehicles[i];
        if(v.kind==VehicleKind::Boat){if(std::abs(v.speed)>1)++boatMoving;require(std::abs(v.position.y-World::WaterLevel)<.8f,"boat left water surface");}
        if(v.kind==VehicleKind::Aircraft){const float altitude=v.position.y-surface(g,v.position);maximumAltitude=std::max(maximumAltitude,altitude);if(altitude>2){if(int(i)==g.occupied)++occupiedAirborne;else ++passiveAirborne;}require(altitude>=-.12f,"aircraft below terrain/water surface");}
    }
    if(frame%300==0){checkMesh(g.dynamicMesh(),"craft dynamic");for(const auto& l:g.lightSources())require(finite(l.position)&&finite(l.color)&&finite(l.direction)&&std::isfinite(l.radius)&&std::isfinite(l.intensity)&&std::isfinite(l.cone),"invalid craft light");++lightChecks;}
    if(frame%1200==0)checkMesh(g.world.combinedMesh(),"craft world");
}
void runBoat(){
    Game g;initialize(g);int index=craftIndex(g,VehicleKind::Boat);placeCraft(g,index,{3100,World::WaterLevel,768},0,0,0,true);
    std::cout<<"START boat occupied/coast/reverse 180 seconds"<<std::endl;
    for(int frame=0;frame<10800;++frame){Input in;const int phase=frame%3600;
        if(phase<2400){in.moveY=.80f;in.moveX=.55f;}else if(phase<2820){in.brake=true;}else if(phase<3240){in.moveY=-.65f;in.moveX=-.35f;}
        in.radio=frame%811==0;g.update(in,Step);++ticks;inspect(g,frame);
        if(frame%1200==1199)roundTrip(g,"boat",frame,true);
        if(frame%1800==1799)std::cout<<"TICK boat seconds="<<(frame+1)/60<<" position="<<vectorText(g.vehicles[size_t(index)].position)<<" health="<<g.vehicles[size_t(index)].health<<" speed="<<g.vehicles[size_t(index)].speed<<std::endl;
    }
    require(boatMoving>7200,"boat fixture spent too little time moving");std::cout<<"PASS boat"<<std::endl;
}
void runAircraft(){
    Game g;initialize(g);int index=craftIndex(g,VehicleKind::Aircraft);auto start=g.vehicles[size_t(index)].position;placeCraft(g,index,start,0,0,0,true);
    std::cout<<"START aircraft runway/takeoff/cruise/bank 180 seconds"<<std::endl;
    for(int frame=0;frame<10800;++frame){Input in;const auto& v=g.vehicles[size_t(index)];const float altitude=v.position.y-surface(g,v.position);
        if(frame<1200){in.moveY=1;in.sprint=true;}else{in.moveY=v.throttle<.74f?.45f:(v.throttle>.77f?-.45f:0);in.moveX=.35f;in.sprint=altitude<165;in.brake=altitude>205;}
        in.lookX=frame%600<60?.001f:0;g.update(in,Step);++ticks;inspect(g,frame);
        if(frame%1200==1199)roundTrip(g,"aircraft",frame,true);
        if(frame%1800==1799)std::cout<<"TICK aircraft seconds="<<(frame+1)/60<<" position="<<vectorText(g.vehicles[size_t(index)].position)<<" health="<<g.vehicles[size_t(index)].health<<" speed="<<g.vehicles[size_t(index)].speed<<std::endl;
    }
    require(occupiedAirborne>9000,"aircraft fixture spent too little time airborne");std::cout<<"PASS aircraft"<<std::endl;
}
void runPassive(int trial){
    Game g;initialize(g);int index=craftIndex(g,VehicleKind::Aircraft);const float x=-3200+trial*180.f,z=-1000-trial*100.f;
    const float height=trial==0?18.f:trial==1?60.f:trial==2?140.f:trial==3?220.f:trial==4?350.f:500.f;
    placeCraft(g,index,{x,g.world.height(x,z)+height,z},.35f*trial,trial==0?0.f:30.f+trial*5,0,false);
    if(trial%2)g.vehicles[size_t(index)].health=0;
    const auto initial=g.vehicles[size_t(index)].position;
    const std::string name="passive-"+std::to_string(trial);std::cout<<"START "<<name<<" 30 seconds position="<<vectorText(initial)<<" health="<<g.vehicles[size_t(index)].health<<std::endl;
    for(int frame=0;frame<1800;++frame){Input in;in.lookX=.001f;g.update(in,Step);++ticks;inspect(g,frame);if(frame==899||frame==1799)roundTrip(g,name,frame,true);}
    const auto& v=g.vehicles[size_t(index)];require(length(v.position-initial)>5,"unoccupied airborne craft froze");
    std::cout<<"PASS "<<name<<" final="<<vectorText(v.position)<<" altitude="<<v.position.y-surface(g,v.position)<<" health="<<v.health<<" parked="<<v.parked<<std::endl;
}
void boundary(const std::string& name,VehicleKind kind,Vec3 position,float yaw,float speed,bool occupied){
    Game g;initialize(g);int index=craftIndex(g,kind);placeCraft(g,index,position,yaw,speed,kind==VehicleKind::Aircraft?.8f:0,occupied);
    if(!occupied){g.player={12,0,12};g.world.stream(g.player);}
    std::cout<<"START "<<name<<" 5 seconds position="<<vectorText(position)<<std::endl;roundTrip(g,name,0,true);
    for(int frame=0;frame<300;++frame){Input in;in.moveY=occupied?.8f:0;in.sprint=kind==VehicleKind::Aircraft&&occupied;g.update(in,Step);++ticks;inspect(g,frame);}
    roundTrip(g,name,300,true);std::cout<<"PASS "<<name<<" final="<<vectorText(g.vehicles[size_t(index)].position)<<std::endl;
}
template<class F>void scenario(const std::string& name,F action){try{action();}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<std::endl;}}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    root=argv[1];auto began=std::chrono::steady_clock::now();
    scenario("boat",runBoat);scenario("aircraft",runAircraft);
    for(int trial=0;trial<6;++trial)scenario("passive-"+std::to_string(trial),[&]{runPassive(trial);});
    scenario("boat-east",[]{boundary("boat-east",VehicleKind::Boat,{6140,World::WaterLevel,0},Pi*.5f,20,true);});
    scenario("boat-south",[]{boundary("boat-south",VehicleKind::Boat,{0,World::WaterLevel,-6140},Pi,20,true);});
    scenario("boat-island",[]{boundary("boat-island",VehicleKind::Boat,{3480,World::WaterLevel,-250},Pi*.5f,18,true);});
    scenario("aircraft-west-passive",[]{boundary("aircraft-west-passive",VehicleKind::Aircraft,{-6140,200,0},-Pi*.5f,55,false);});
    scenario("aircraft-north",[]{boundary("aircraft-north",VehicleKind::Aircraft,{0,200,6140},0,55,true);});
    scenario("aircraft-ceiling",[]{boundary("aircraft-ceiling",VehicleKind::Aircraft,{-3200,1149,-1000},0,80,true);});
    const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
    std::cout<<"SUMMARY ticks="<<ticks<<" simulated_seconds="<<double(ticks)/60<<" meshes="<<meshes<<" round_trips="<<roundTrips<<" light_checks="<<lightChecks<<" boat_moving_seconds="<<double(boatMoving)/60<<" occupied_airborne_seconds="<<double(occupiedAirborne)/60<<" passive_airborne_seconds="<<double(passiveAirborne)/60<<" maximum_altitude="<<maximumAltitude<<" failures="<<failures<<" wall_seconds="<<wall<<std::endl;
    return failures?1:0;
}
