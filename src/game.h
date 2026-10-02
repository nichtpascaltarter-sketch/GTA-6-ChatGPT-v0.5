#pragma once
#include "world.h"
#include <string>
#include <vector>

namespace mc {
struct Input {
    float moveX=0,moveY=0,lookX=0,lookY=0;
    bool sprint=false,brake=false,fire=false,aim=false;
    bool interact=false,reload=false,mission=false,radio=false,jump=false;
};
enum class VehicleKind {Car,Motorcycle,Boat,Aircraft};
struct Vehicle {
    Vec3 position; float yaw=0,speed=0,steer=0; Vec3 velocity;
    Vec3 color{.2f,.45f,.65f}; VehicleKind kind=VehicleKind::Car;
    bool police=false,parked=false; float health=100;
    float pitch=0,roll=0,throttle=0;
};
struct Pedestrian {Vec3 position;float yaw=0,phase=0,panic=0,health=100;};
struct Mission { const char* title;const char* briefing;Vec3 start,target;int reward;};
struct Game {
    World world;
    Vec3 player{8,0,8}; float yaw=0,pitch=.20f,health=100; int money=250,ammo=30,reserveAmmo=120,wanted=0;
    int occupied=-1,activeMission=-1,missionStage=0,completedMissions=0,radioStation=1;
    float time=0,dayTime=17.2f,rain=0,missionTimer=0,shotFlash=0;
    Vec3 shotEnd; bool paused=false;std::string message;float messageTime=0;
    std::vector<Vehicle> vehicles;std::vector<Pedestrian> pedestrians;
    void initialize();
    void update(const Input&,float dt);
    Mesh dynamicMesh() const;
    std::vector<Light> lightSources() const;
    Vec3 cameraEye() const; Vec3 cameraTarget() const;
    Vec3 missionTarget() const;
    const Mission* missionInfo() const;
    const char* missionInstruction() const;
    static const std::vector<Mission>& missions();
    bool save(const std::string& path) const;
    bool load(const std::string& path);
private:
    float fireCooldown=0,wantedTimer=0,verticalSpeed=0,missionHold=0;
    bool grounded=true;float populationTimer=0;
    float reloadTimer=0,invulnerabilityTimer=0,playerPhase=0,playerMotion=0,cameraFollowDelay=0;
    bool wasInteract=false,wasReload=false,wasMission=false,wasRadio=false,wasJump=false,aiming=false;
    std::vector<Vec3> trafficTargets,pedestrianTargets;
    uint32_t simulationTick=0;
    Vec3 shotOrigin;
};
}
