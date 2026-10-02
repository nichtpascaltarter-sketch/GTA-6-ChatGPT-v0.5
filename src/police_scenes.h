#pragma once
#include "game.h"
#include "resident_scenes.h"
#include <chrono>
#include <ostream>

namespace mc {
// Inspection advances an untouched population after one actual player shot.
// The captured aim, muzzle event and reload all come from the law simulation.
struct PoliceCapture {
    Vec3 eye{},target{};
    uint32_t identity=0,shots=0;
    bool active=false;
    PolicePose frozenPose{};
    Vec3 frozenPosition{};
    float frozenTime=0;
    size_t frozenShotCount=0;

    bool retained(const Game& game)const {
        if(!active||game.pedestrians.empty()||!game.paused||game.time!=frozenTime||
           game.pedestrians[0].identity!=identity||game.lawShots().size()!=frozenShotCount)return false;
        const auto same=[](Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
        const auto pose=game.policePose(0);
        return same(game.pedestrians[0].position,frozenPosition)&&pose.phase==frozenPose.phase&&
               pose.aim==frozenPose.aim&&pose.reload==frozenPose.reload&&pose.flash==frozenPose.flash&&
               pose.reloading==frozenPose.reloading&&same(pose.aimPoint,frozenPose.aimPoint)&&
               same(pose.muzzle,frozenPose.muzzle)&&same(pose.impact,frozenPose.impact);
    }

    template<class Pump>
    bool prepare(Game& game,const std::string& scene,std::ostream& log,std::string& error,Pump pump){
        if(scene!="police-aim"&&scene!="police-fire"&&scene!="police-reload"){
            error="Unknown police inspection scene.";return false;
        }
        if(game.pedestrians.size()<4){error="Police inspection requires the normal officer roster.";return false;}
        const uint32_t expectedIdentity=game.pedestrians[0].identity;
        game.player=game.pedestrians[0].position+Vec3{0,0,20};
        game.player.y=game.world.height(game.player.x,game.player.z);
        game.world.stream(game.player);
        if(game.world.blocked(game.player,.35f)){error="Police inspection player approach is obstructed.";return false;}
        game.yaw=0;game.pitch=-.8f;game.dayTime=14;game.rain=0;game.paused=false;
        const int ammunition=game.ammo;
        Input fire;fire.aim=fire.fire=true;game.update(fire,1.f/60,false);
        if(game.ammo!=ammunition-1||game.shotFlash<=0){error="Police inspection did not emit its initiating shot.";return false;}
        const auto began=std::chrono::steady_clock::now();
        unsigned steps=1;bool reached=false;
        for(;steps<1200;++steps){
            if(steps%30==1){
                if(!pump()){error="Police inspection interrupted while advancing simulation.";return false;}
                if(std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()>60){error="Police inspection exceeded its wall-time budget.";return false;}
            }
            game.update({},1.f/60,false);
            const auto& person=game.pedestrians[0];
            if(person.identity!=expectedIdentity||person.health<=0||game.health<=0){error="Police inspection lost its participants.";return false;}
            const auto pose=game.policePose(0);const auto& state=game.lawState();
            const LawUnitState* officer=nullptr;
            for(uint32_t i=0;i<state.count;++i)if(state.units[i].identity==1)officer=&state.units[i];
            if(!officer){error="Police inspection lost its law binding.";return false;}
            bool emitted=false;for(const auto& shot:game.lawShots())if(shot.shooter==officer->identity&&shot.sequence==officer->weapon.shotsFired)emitted=true;
            reached=scene=="police-aim"?(pose.aim>=.8f&&officer->weapon.shotsFired==0):scene=="police-fire"?(pose.flash>0&&emitted):(pose.reloading&&pose.reload>=.35f&&pose.reload<=.6f&&officer->weapon.shotsFired==6);
            if(reached){identity=person.identity;shots=officer->weapon.shotsFired;++steps;break;}
        }
        if(!reached){error="Police inspection did not reach the requested state naturally.";return false;}
        const auto& person=game.pedestrians[0];const auto pose=game.policePose(0);
        target=person.position+Vec3{0,scene=="police-reload"?1.05f:1.2f,0};
        const Mesh dynamics=game.dynamicMesh();bool view=false;
        for(float distance:{3.95f,5.0f,6.0f}){
            for(float angle:{.623f,-.623f,1.0f,-1.0f,.35f,-.35f}){
                const Vec3 candidate=person.position+forward(person.yaw+angle)*distance+Vec3{0,1.8f,0};
                bool clear=true;
                for(float height:{1.65f,1.15f}){
                    const Vec3 point=pedestrianBodyPoint(person,height);
                    clear=clear&&ResidentCapture::clear(game.world,point,candidate)&&ResidentCapture::clearGeometry(game.world,point,candidate)&&ResidentCapture::clearDynamic(dynamics,point,candidate);
                }
                if(clear){eye=candidate;view=true;break;}
            }
            if(view)break;
        }
        if(!view){error="Police inspection has no unobstructed camera.";return false;}
        game.messageTime=0;game.paused=true;active=true;
        frozenPose=pose;frozenPosition=person.position;frozenTime=game.time;frozenShotCount=game.lawShots().size();
        log<<"Police capture: scene="<<scene<<"; identity="<<identity<<"; shots="<<shots<<"; warmup="<<steps/60.f
           <<"; health="<<game.health<<"; wanted="<<game.wanted<<"; aim="<<pose.aim<<"; reload="<<pose.reload<<"; flash="<<pose.flash
           <<"; people="<<game.pedestrians.size()<<"; vehicles="<<game.vehicles.size()
           <<"; position="<<person.position.x<<','<<person.position.y<<','<<person.position.z
           <<"; eye="<<eye.x<<','<<eye.y<<','<<eye.z<<"; target="<<target.x<<','<<target.y<<','<<target.z<<'\n';
        return true;
    }
};
}
