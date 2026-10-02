#pragma once
#include "game.h"
#include <chrono>
#include <ostream>

namespace mc {
// Native inspection scenes advance the real simulation before freezing a view.
// No activity, pose, route, reservation, or civilian position is assigned here.
struct ResidentCapture {
    Vec3 eye{},target{};
    uint32_t identity=0;
    bool active=false;

    static Vec3 center(const Pedestrian& p){
        return pedestrianBodyPoint(p,1.15f);
    }
    static bool clear(const World& world,Vec3 from,Vec3 to){
        const Vec3 delta=to-from;
        for(const auto& chunk:world.chunks)for(const auto& box:chunk.solids){
            if(to.x>box.min.x-.05f&&to.x<box.max.x+.05f&&to.y>box.min.y-.05f&&to.y<box.max.y+.05f&&to.z>box.min.z-.05f&&to.z<box.max.z+.05f)return false;
            float first=0,last=1;bool intersects=true;
            const float origin[]={from.x,from.y,from.z},direction[]={delta.x,delta.y,delta.z};
            const float lo[]={box.min.x-.05f,box.min.y-.05f,box.min.z-.05f};
            const float hi[]={box.max.x+.05f,box.max.y+.05f,box.max.z+.05f};
            for(int axis=0;axis<3;++axis){
                if(std::abs(direction[axis])<.000001f){if(origin[axis]<lo[axis]||origin[axis]>hi[axis]){intersects=false;break;}}
                else {float a=(lo[axis]-origin[axis])/direction[axis],b=(hi[axis]-origin[axis])/direction[axis];if(a>b)std::swap(a,b);first=std::max(first,a);last=std::min(last,b);if(first>last){intersects=false;break;}}
            }
            if(intersects&&first<.99f&&last>.01f)return false;
        }
        return true;
    }
    static bool clearMesh(const Mesh& mesh,Vec3 from,Vec3 to,float nearIgnore){
        const float distance=length(to-from);if(distance<=nearIgnore)return false;
        const Vec3 direction=(to-from)/distance;
        for(size_t i=0;i+2<mesh.indices.size();i+=3){
            const Vec3 a=mesh.vertices[mesh.indices[i]].position;
            const Vec3 edge1=mesh.vertices[mesh.indices[i+1]].position-a,edge2=mesh.vertices[mesh.indices[i+2]].position-a;
            const Vec3 normal=cross(direction,edge2);const float determinant=dot(edge1,normal);
            if(std::abs(determinant)<.000001f)continue;
            const Vec3 offset=from-a;const float u=dot(offset,normal)/determinant;
            if(u<0||u>1)continue;
            const Vec3 side=cross(offset,edge1);const float v=dot(direction,side)/determinant;
            if(v<0||u+v>1)continue;
            const float hit=dot(edge2,side)/determinant;
            if(hit>nearIgnore&&hit<distance-.03f)return false;
        }
        return true;
    }
    static bool clearDynamic(const Mesh& mesh,Vec3 from,Vec3 to){
        // Skip only the inspected person's own face, hands and held prop.
        return clearMesh(mesh,from,to,.4f);
    }
    static bool clearGeometry(const World& world,Vec3 from,Vec3 to){
        for(const auto& chunk:world.chunks){
            const auto& b=chunk.bounds;
            if(std::max(from.x,to.x)<b.min.x||std::min(from.x,to.x)>b.max.x||
               std::max(from.y,to.y)<b.min.y||std::min(from.y,to.y)>b.max.y||
               std::max(from.z,to.z)<b.min.z||std::min(from.z,to.z)>b.max.z)continue;
            // Market goods, awnings and other visible decorations need not be
            // collision solids, but must still block an inspection sightline.
            if(!clearMesh(chunk.mesh,from,to,.02f))return false;
        }
        return true;
    }
    static int find(const Game& game,PedestrianActivity activity,Vec3* nearby=nullptr){
        for(size_t i=4;i<game.pedestrians.size();++i){
            const auto& p=game.pedestrians[i];
            if(p.health<=0||p.activity!=activity||(nearby&&length(p.position-*nearby)>35))continue;
            if(activity==PedestrianActivity::Sit&&p.sitBlend<.95f)continue;
            if(activity==PedestrianActivity::Carry&&!p.carrying)continue;
            if(activity==PedestrianActivity::Talk){
                bool partner=false;for(size_t j=4;j<game.pedestrians.size();++j)if(i!=j){const auto& other=game.pedestrians[j];if(other.health>0&&other.activity==activity&&length(other.position-p.position)<5.5f)partner=true;}
                if(!partner)continue;
            }
            return int(i);
        }
        return -1;
    }
    static bool spectator(Game& game,Vec3 at,float heading){
        for(float distance:{10.f,7.f,14.f})for(float angle:{0.f,.7f,-.7f,1.4f,-1.4f,Pi}){
            Vec3 p=at+forward(heading+angle)*distance;p.y=game.world.height(p.x,p.z);
            if(game.world.collisionReady(p)&&!game.world.blocked(p,.35f)){game.player=p;return true;}
        }
        return false;
    }
    template<class Pump>
    bool prepare(Game& game,const std::string& scene,std::ostream& log,std::string& error,Pump pump){
        const bool alarm=scene=="residents-startle"||scene=="residents-flee";
        if(!alarm&&scene!="residents-carry"&&scene!="residents-work"&&scene!="residents-bench"&&scene!="residents-talk"){
            error="Unknown resident inspection scene.";return false;
        }
        const auto goal=scene=="residents-carry"?PedestrianActivity::Carry:scene=="residents-work"?PedestrianActivity::Work:scene=="residents-bench"?PedestrianActivity::Sit:PedestrianActivity::Talk;
        game.dayTime=goal==PedestrianActivity::Carry||goal==PedestrianActivity::Work?8.f:goal==PedestrianActivity::Sit?12.f:18.f;
        game.paused=false;
        const auto began=std::chrono::steady_clock::now();
        int selected=-1,previous=-1,stable=0;unsigned steps=0;Vec3 motionStart{};float displacement=0;
        auto service=[&](){
            if(!pump()){error="Resident scene interrupted while advancing simulation.";return false;}
            if(std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()>60){error="Resident scene simulation exceeded its wall-time budget.";return false;}
            return true;
        };
        for(;steps<5400;++steps){
            if(steps%30==0&&!service())return false;
            game.update({},1.f/30,false);selected=find(game,goal);
            if(selected>=0){
                if(selected==previous)++stable;else {stable=0;motionStart=game.pedestrians[size_t(selected)].position;}
                displacement=length(game.pedestrians[size_t(selected)].position-motionStart);
            }else stable=0;
            previous=selected;
            if(stable>=30&&(goal!=PedestrianActivity::Carry||(displacement>=.4f&&game.pedestrians[size_t(selected)].motion>.25f)))break;
        }
        if(steps>=5400||selected<0||stable<30){error="Resident scene did not reach its requested activity naturally.";return false;}
        ++steps; // Include the successful update before the loop's increment.
        const Vec3 gathering=game.pedestrians[size_t(selected)].position;
        if(alarm){
            if(!spectator(game,gathering,game.pedestrians[size_t(selected)].yaw)){error="No clear spectator position for the resident alarm.";return false;}
            const int calmIndex=selected,rounds=game.ammo;const uint32_t calmIdentity=game.pedestrians[size_t(selected)].identity;
            game.yaw=0;game.pitch=-.8f;Input shot;shot.aim=shot.fire=true;
            game.update(shot,1.f/30,false);++steps;
            if(game.ammo!=rounds-1||game.shotFlash<=0){error="Resident alarm did not emit its required gunshot.";return false;}
            const auto reaction=scene=="residents-startle"?PedestrianActivity::Startle:PedestrianActivity::Flee;
            stable=0;selected=-1;motionStart=game.pedestrians[size_t(calmIndex)].position;displacement=0;
            for(unsigned step=0;step<180;++step){
                if(step%30==0&&!service())return false;
                game.update({},1.f/30,false);++steps;
                const auto& responding=game.pedestrians[size_t(calmIndex)];
                if(responding.identity==calmIdentity&&responding.health>0&&responding.activity==reaction){selected=calmIndex;++stable;}
                else {selected=-1;stable=0;}
                displacement=length(responding.position-motionStart);
                if(stable>=(reaction==PedestrianActivity::Startle?3:16)&&(reaction!=PedestrianActivity::Flee||(displacement>=.5f&&responding.motion>.4f)))break;
            }
            if(selected<0||stable<(reaction==PedestrianActivity::Startle?3:16)||(reaction==PedestrianActivity::Flee&&(displacement<.5f||game.pedestrians[size_t(selected)].motion<=.4f))){error="The selected calm resident did not react to the actual gunshot.";return false;}
        }
        const auto& person=game.pedestrians[size_t(selected)];identity=person.identity;target=center(person);
        std::array<int,2> subjects{selected,-1};
        if(person.activity==PedestrianActivity::Talk){
            for(size_t i=4;i<game.pedestrians.size();++i){const auto& other=game.pedestrians[i];if(i!=size_t(selected)&&other.health>0&&other.activity==PedestrianActivity::Talk&&length(other.position-person.position)<5.5f){target=(target+center(other))*.5f;subjects[1]=int(i);break;}}
        }
        if(!spectator(game,target,person.yaw+.55f)){error="Resident scene has no clear nearby spectator position.";return false;}
        game.world.stream(game.player);
        bool view=false;
        const Mesh dynamics=game.dynamicMesh();
        auto visiblePoint=[&](Vec3 point,Vec3 candidate){return clear(game.world,point,candidate)&&clearGeometry(game.world,point,candidate)&&clearDynamic(dynamics,point,candidate);};
        const bool stockCheck=person.activity==PedestrianActivity::Work;
        const std::array<float,3> distances=stockCheck?std::array{3.4f,4.5f,5.5f}:std::array{5.5f,4.f,7.f};
        for(float distance:distances){
            for(float angle:{.55f,-.55f,0.f,1.1f,-1.1f}){
                Vec3 candidate=target+forward(person.yaw+angle)*distance+Vec3{0,stockCheck?.65f:1.1f,0};
                candidate.y=std::max(candidate.y,game.world.height(candidate.x,candidate.z)+1.5f);
                bool visible=true;
                for(int index:subjects)if(index>=0){
                    const auto& subject=game.pedestrians[size_t(index)];
                    visible=visible&&visiblePoint(pedestrianBodyPoint(subject,1.68f),candidate)&&visiblePoint(center(subject),candidate);
                    if(subject.carrying)visible=visible&&visiblePoint(pedestrianBodyPoint(subject,1.f)+forward(subject.yaw)*.4f,candidate);
                    else if(subject.activity==PedestrianActivity::Work)visible=visible&&visiblePoint(pedestrianBodyPoint(subject,1.115f)+forward(subject.yaw)*.44f,candidate);
                }
                if(visible){eye=candidate;view=true;break;}
            }
            if(view)break;
        }
        if(!view){error="Resident scene has no unobstructed inspection camera.";return false;}
        game.messageTime=0;game.paused=true;active=true;
        const auto stats=game.pedestrianStats();
        log<<"Resident capture: scene="<<scene<<"; identity="<<identity<<"; activity="<<Game::pedestrianActivityName(person.activity)
           <<"; warmup="<<steps/30.f<<"; hour="<<game.dayTime<<"; people="<<game.pedestrians.size()<<"; residents="<<stats.persistentResidents
           <<"; groups="<<stats.activeGroups<<"; displacement="<<displacement<<"; position="<<person.position.x<<','<<person.position.y<<','<<person.position.z
           <<"; eye="<<eye.x<<','<<eye.y<<','<<eye.z<<"; target="<<target.x<<','<<target.y<<','<<target.z<<'\n';
        return true;
    }
};
}
