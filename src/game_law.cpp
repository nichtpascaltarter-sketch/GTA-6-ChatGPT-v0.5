#include "game.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mc {
namespace {
float planar(Vec3 a,Vec3 b){a.y=b.y=0;return length(a-b);}
float gridAt(Vec3 p){if(p.x>=-1664&&p.x<=1536&&p.z>=-1792&&p.z<=2048)return 128;if(p.x>=-3072&&p.x<=2048&&p.z>=-3072&&p.z<=3072)return 256;return 512;}
float hitBox(Vec3 o,Vec3 d,Vec3 lo,Vec3 hi,float maximum){
    float near=0,far=maximum;const float origin[]{o.x,o.y,o.z},direction[]{d.x,d.y,d.z},minimum[]{lo.x,lo.y,lo.z},limit[]{hi.x,hi.y,hi.z};
    for(int i=0;i<3;++i){if(std::abs(direction[i])<1e-6f){if(origin[i]<minimum[i]||origin[i]>limit[i])return maximum;}
        else {float a=(minimum[i]-origin[i])/direction[i],b=(limit[i]-origin[i])/direction[i];if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);if(near>far)return maximum;}}
    return near;
}
float hitSphere(Vec3 origin,Vec3 direction,Vec3 center,float radius,float maximum){
    const Vec3 offset=origin-center;const float b=dot(offset,direction),c=dot(offset,offset)-radius*radius,disc=b*b-c;
    if(c<=0)return 0;
    if(disc<0)return maximum;
    const float t=-b-std::sqrt(disc);return t>=0&&t<maximum?t:maximum;
}
float hitPerson(Vec3 origin,Vec3 direction,const Pedestrian& person,float maximum){
    for(float height:{.35f,.65f,.95f,1.22f})maximum=std::min(maximum,hitSphere(origin,direction,pedestrianBodyPoint(person,height),.25f,maximum));
    return std::min(maximum,hitSphere(origin,direction,pedestrianBodyPoint(person,1.62f),.17f,maximum));
}
Vec3 vehicleLocal(const Vehicle& v,Vec3 vector){
    Vec3 p{dot(vector,right(v.yaw)),vector.y,dot(vector,forward(v.yaw))};
    const float cp=std::cos(v.pitch),sp=std::sin(v.pitch),cr=std::cos(v.roll),sr=std::sin(v.roll);
    p={p.x,p.y*cp-p.z*sp,p.y*sp+p.z*cp};return {p.x*cr+p.y*sr,-p.x*sr+p.y*cr,p.z};
}
float hitVehicle(Vec3 origin,Vec3 direction,const Vehicle& v,float maximum,float radius=0,bool standing=false){
    const Vec3 o=vehicleLocal(v,origin-v.position),d=vehicleLocal(v,direction);
    const auto box=[&](Vec3 lo,Vec3 hi){lo.x-=radius;lo.z-=radius;hi.x+=radius;hi.z+=radius;if(standing)lo.y-=1.7f;maximum=std::min(maximum,hitBox(o,d,lo,hi,maximum));};
    switch(v.kind){
    case VehicleKind::Car:box({-1,.25f,-2.25f},{1,.96f,2.3f});box({-.83f,.96f,-1.15f},{.83f,1.6f,1.1f});break;
    case VehicleKind::Motorcycle:box({-.35f,.2f,-1.1f},{.35f,1.05f,1.1f});break;
    case VehicleKind::Boat:box({-1.1f,-.35f,-2.8f},{1.1f,.65f,3});box({-.65f,.65f,-.4f},{.65f,1.5f,.55f});break;
    case VehicleKind::Aircraft:box({-.58f,.32f,-3.7f},{.58f,1.95f,3.7f});box({-4.9f,1.08f,-.75f},{4.9f,1.5f,1.05f});box({-1.85f,.9f,-3.3f},{1.85f,1.27f,-2.4f});break;
    }
    return maximum;
}
class GameLawSpace final:public LawSpace {
public:
    explicit GameLawSpace(const Game& game,std::span<const uint64_t> observerVehicles={}):game_(game),world_(game.world),observerVehicles_(observerVehicles){}
    void setObserver(uint32_t identity)const override{observer_=identity;}
    bool ready(Vec3 p)const override{return world_.ready(p);}
    bool project(Vec3 p,float radius,Vec3& ground)const override {
        if(radius<1)return world_.project(p,radius,ground);
        const float grid=gridAt(p),x=std::round(p.x/grid)*grid,z=std::round(p.z/grid)*grid;
        const std::array<Vec3,4> candidates{{{x+3.2f,p.y,p.z},{x-3.2f,p.y,p.z},{p.x,p.y,z+3.2f},{p.x,p.y,z-3.2f}}};
        float nearest=std::numeric_limits<float>::infinity();bool found=false;
        for(Vec3 candidate:candidates){Vec3 foot;if(game_.world.road(candidate.x,candidate.z)&&world_.project(candidate,radius,foot)&&planar(p,foot)<nearest){ground=foot;nearest=planar(p,foot);found=true;}}
        return found;
    }
    bool walkClear(Vec3 a,Vec3 b,float radius)const override{
        if(!world_.walkClear(a,b,radius))return false;
        const float distance=length(b-a);if(distance<.001f)return true;
        for(const auto& vehicle:game_.vehicles)if(hitVehicle(a,(b-a)/distance,vehicle,distance,radius,true)<distance-.001f)return false;
        return true;
    }
    size_t obstacles(Vec3 c,float r,std::span<LawObstacle> out)const override{
        size_t count=world_.obstacles(c,r,out);
        const auto distance=[c](const LawObstacle& box){const Vec3 closest{clamp(c.x,box.min.x,box.max.x),c.y,clamp(c.z,box.min.z,box.max.z)};return planar(c,closest);};
        for(const auto& vehicle:game_.vehicles){
            const float width=vehicle.kind==VehicleKind::Aircraft?5.0f:vehicle.kind==VehicleKind::Boat?1.2f:vehicle.kind==VehicleKind::Motorcycle?.42f:1.05f;
            const float extent=vehicle.kind==VehicleKind::Aircraft?3.8f:vehicle.kind==VehicleKind::Boat?3.1f:vehicle.kind==VehicleKind::Motorcycle?1.2f:2.35f;
            const float x=std::abs(std::cos(vehicle.yaw))*width+std::abs(std::sin(vehicle.yaw))*extent,z=std::abs(std::sin(vehicle.yaw))*width+std::abs(std::cos(vehicle.yaw))*extent;
            const LawObstacle candidate{vehicle.position-Vec3{x,.35f,z},vehicle.position+Vec3{x,2.1f,z}};
            if(c.y>=candidate.max.y||c.y+1.7f<=candidate.min.y||distance(candidate)>r)continue;
            size_t insert=0;while(insert<count&&distance(out[insert])<=distance(candidate))++insert;
            if(insert>=out.size())continue;
            const size_t end=std::min(count,out.size()-1);for(size_t i=end;i>insert;--i)out[i]=out[i-1];
            out[insert]=candidate;count=std::min(count+1,out.size());
        }
        return count;
    }
    bool lineClear(Vec3 from,Vec3 to)const override {
        if(!world_.lineClear(from,to))return false;
        const float distance=length(to-from);if(distance<.001f)return true;const Vec3 direction=(to-from)/distance;
        for(size_t i=0;i<game_.vehicles.size();++i){
            const auto& v=game_.vehicles[i];
            // A target occupant is observed as their vehicle. Its body remains
            // opaque to the final emitted shot; it cannot reveal a hidden foot actor.
            bool ownVehicle=false;
            for(size_t unit=0;unit<observerVehicles_.size();++unit)if(game_.lawState().units[unit].identity==observer_&&observerVehicles_[unit]==v.identity)ownVehicle=true;
            if(int(i)==game_.occupied||ownVehicle)continue;
            if(hitVehicle(from,direction,v,distance)<distance-.05f)return false;
        }
        for(size_t i=0;i<game_.pedestrians.size();++i){
            const auto& p=game_.pedestrians[i];
            if(p.health<=0||(i<4&&observer_==uint32_t(i+1)))continue;
            if(hitPerson(from,direction,p,distance)<distance-.05f)return false;
        }
        return true;
    }
private:
    const Game& game_;WorldLawSpace world_;std::span<const uint64_t> observerVehicles_;mutable uint32_t observer_=0;
};
}

LawState Game::lawSnapshot(std::array<LawBinding,LawMaxUnits>& bindings)const {
    LawState result=law.state();result.units={};result.count=0;bindings={};
    const auto append=[&](LawBinding binding,LawUnitKind kind,Vec3 home){
        const uint32_t n=result.count++;bindings[n]=binding;auto& unit=result.units[n];
        unit.identity=binding.logical;unit.kind=kind;unit.home=unit.goal=home;
        if(kind==LawUnitKind::Patrol)unit.weapon.rounds=0;
        for(uint32_t old=0;old<law.state().count;++old){
            const auto& prior=lawBindings[old];
            if(prior.logical==binding.logical&&prior.pedestrian==binding.pedestrian&&prior.vehicle==binding.vehicle){unit=law.state().units[old];break;}
            if(prior.logical==binding.logical&&kind==LawUnitKind::Officer)unit.weapon.shotsFired=law.state().units[old].weapon.shotsFired;
        }
    };
    for(size_t i=0;i<std::min(size_t(4),pedestrians.size());++i)append({uint32_t(i+1),pedestrians[i].identity,0},LawUnitKind::Officer,pedestrians[i].position);
    std::array<bool,4> occupiedSlot{};
    std::array<size_t,4> selected{};size_t count=0;
    for(size_t i=0;i<vehicles.size()&&count<4;++i)if(vehicles[i].police&&(vehicles[i].kind==VehicleKind::Car||vehicles[i].kind==VehicleKind::Motorcycle))selected[count++]=i;
    for(size_t entry=0;entry<count;++entry){
        const auto& v=vehicles[selected[entry]];uint32_t logical=0;
        for(uint32_t old=0;old<law.state().count;++old)if(lawBindings[old].vehicle&&lawBindings[old].vehicle==v.identity&&lawBindings[old].logical>=101&&lawBindings[old].logical<=104){logical=lawBindings[old].logical;occupiedSlot[logical-101]=true;break;}
        if(logical)append({logical,0,v.identity},LawUnitKind::Patrol,v.position);
    }
    for(size_t entry=0;entry<count;++entry){
        const auto& v=vehicles[selected[entry]];bool found=false;
        for(uint32_t n=0;n<result.count;++n)if(bindings[n].vehicle&&bindings[n].vehicle==v.identity)found=true;
        if(found)continue;
        for(uint32_t slot=0;slot<4;++slot)if(!occupiedSlot[slot]){occupiedSlot[slot]=true;append({101+slot,0,v.identity},LawUnitKind::Patrol,v.position);break;}
    }
    const auto observerExists=[&](uint32_t identity){if(!identity)return true;for(uint32_t i=0;i<result.count;++i)if(result.units[i].identity==identity)return true;return false;};
    if(!observerExists(result.shared.observer))result.shared={};
    result.decisionCursor%=std::max(1u,result.count);result.weaponCursor%=std::max(1u,result.count);
    result.wanted=uint8_t(std::clamp(wanted,0,5));
    if(result.wanted!=law.state().wanted) {
        result.alertRemaining=result.wanted?clamp(wantedTimer>0?wantedTimer:22,0,600):0;
        if(!result.wanted){result.shared={};result.searchedSlots=0;for(uint32_t i=0;i<result.count;++i){auto& u=result.units[i];u.memory={};u.radio={};u.sightExposure=0;u.weapon.aimTime=0;u.hasGoal=false;u.searchSlot=-1;u.scanTime=0;u.searchSerial=0;}}
    }
    return result;
}
void Game::synchronizeLaw(){
    std::array<LawBinding,LawMaxUnits> bindings;const LawState next=lawSnapshot(bindings);
    bool changed=next.count!=law.state().count||next.wanted!=law.state().wanted;
    for(uint32_t i=0;i<next.count&&!changed;++i)changed=bindings[i].logical!=lawBindings[i].logical||bindings[i].pedestrian!=lawBindings[i].pedestrian||bindings[i].vehicle!=lawBindings[i].vehicle;
    if(changed){law.restore(next);lawBindings=bindings;lawFlashes={};}
    lawEvidenceSerial=std::max(lawEvidenceSerial,law.state().lastEvidenceSerial);
}
void Game::resetLaw(){
    const LawState prior=law.state();
    law=LawSystem{};lawBindings={};lawFlashes={};lawEvidenceSerial=lawObserverCursor=0;lawHasPreviousPlayer=false;synchronizeLaw();
    LawState reset=law.state();
    for(uint32_t i=0;i<reset.count;++i)for(uint32_t old=0;old<prior.count;++old)if(reset.units[i].identity==prior.units[old].identity){
        reset.units[i].home=prior.units[old].home;
        reset.units[i].weapon.shotsFired=prior.units[old].weapon.shotsFired;
    }
    law.restore(reset);
}
void Game::suspendLaw(){
    GameLawSpace space(*this);law.update({0,true,{}},space);lawFlashes={};lawHasPreviousPlayer=false;
}
void Game::reportCrime(Vec3 origin,int severity,float duration,bool audible){
    if(lawEvidenceSerial==std::numeric_limits<uint32_t>::max())return;
    LawEvidence evidence;evidence.serial=++lawEvidenceSerial;evidence.kind=audible?LawEvidenceKind::Gunshot:LawEvidenceKind::Report;
    evidence.position=origin;evidence.uncertainty=audible?8.0f:2.0f;evidence.severity=uint8_t(std::clamp(severity,1,5));evidence.alertSeconds=duration;
    if(!audible){law.alert(evidence.severity,duration);wanted=law.state().wanted;wantedTimer=law.state().alertRemaining;}
    law.report(evidence);
}
void Game::reportWitnessedCrime(Vec3 origin,int severity,float duration){
    // A collision without a reporting radio is evidence only for an actual law
    // witness. The core still checks this frozen event's angle, range and cover.
    for(uint32_t i=0;i<law.state().count;++i){
        if(lawEvidenceSerial==std::numeric_limits<uint32_t>::max())break;
        LawEvidence evidence;evidence.serial=++lawEvidenceSerial;evidence.observer=law.state().units[i].identity;
        evidence.kind=LawEvidenceKind::Sight;evidence.position=origin+Vec3{0,1.1f,0};
        evidence.severity=uint8_t(std::clamp(severity,1,5));evidence.alertSeconds=duration;law.report(evidence);
    }
}
bool Game::patrolLawTarget(size_t vehicleIndex,Vec3& target,float& speed){
    const Vehicle& vehicle=vehicles[vehicleIndex];
    for(uint32_t unit=0;unit<law.state().count;++unit)if(lawBindings[unit].vehicle==vehicle.identity&&vehicle.identity){
        const auto& state=law.state().units[unit];const auto& command=law.commands()[unit];
        if(state.phase==LawPhase::Patrol)return false;
        speed=command.speed;
        if(!state.hasGoal||speed<=0){target=vehicle.position;speed=0;return true;}
        const Vec3 goal=state.goal;const float grid=gridAt(vehicle.position);
        target=trafficTargets[vehicleIndex];
        if(planar(vehicle.position,goal)<8){target=vehicle.position;speed=0;return true;}
        const Vec3 toGoal=goal-vehicle.position;
        if(dot(toGoal,forward(vehicle.yaw))>0&&dot(toGoal,forward(vehicle.yaw))<grid&&std::abs(dot(toGoal,right(vehicle.yaw)))<6){target=goal;return true;}
        if(planar(vehicle.position,target)<11||planar(vehicle.position,target)>grid*1.8f){
            const Vec3 node{std::round(vehicle.position.x/grid)*grid,0,std::round(vehicle.position.z/grid)*grid};
            const Vec3 delta=goal-node;Vec3 direction;
            if(std::abs(delta.x)>std::abs(delta.z))direction={delta.x>=0?1.0f:-1.0f,0,0};else direction={0,0,delta.z>=0?1.0f:-1.0f};
            const float yaw=std::atan2(direction.x,direction.z);
            target=node+direction*grid+right(yaw)*3.2f;target.y=world.height(target.x,target.z);trafficTargets[vehicleIndex]=target;
        }
        return true;
    }
    return false;
}
PolicePose Game::policePose(size_t pedestrianIndex)const {
    PolicePose pose;if(pedestrianIndex>=pedestrians.size()||pedestrians[pedestrianIndex].health<=0)return pose;
    const uint32_t identity=pedestrians[pedestrianIndex].identity;
    for(uint32_t i=0;i<law.state().count;++i)if(lawBindings[i].pedestrian==identity&&identity){
        const auto& state=law.state().units[i];const auto& command=law.commands()[i];pose.phase=state.phase;
        pose.aim=command.aim?clamp(state.weapon.aimTime/LawSystem::AimSeconds,0,1):0;
        pose.reloading=state.weapon.reload>0;
        pose.reload=pose.reloading?1-state.weapon.reload/LawSystem::ReloadSeconds:0;
        pose.aimPoint=command.hasLookTarget?command.lookTarget:pedestrians[pedestrianIndex].position+Vec3{0,1.35f,0}+forward(pedestrians[pedestrianIndex].yaw)*20;
        const Vec3 body=pedestrians[pedestrianIndex].position+Vec3{0,1.35f,0};pose.muzzle=body+normalized(pose.aimPoint-body)*.45f;
        for(const auto& flash:lawFlashes)if(flash.logical==state.identity&&flash.remaining>0){pose.flash=flash.remaining/.075f;pose.muzzle=flash.muzzle;pose.impact=flash.impact;}
        break;
    }
    return pose;
}
void Game::recoverLawStarts(){
    // Older saves and newly resident geometry can contain a small pre-existing
    // overlap. Resolve it before asking the navigation graph for a valid start;
    // otherwise its correct rejection would prevent World::move ever running.
    for(size_t i=0;i<std::min(size_t(4),pedestrians.size());++i){
        auto& p=pedestrians[i];
        if(p.health<=0||!world.collisionReady(p.position)||!world.blocked(p.position,.35f))continue;
        const Vec3 old=p.position,candidate=world.move(old,{},.35f);
        if(length(candidate-old)>1||world.blocked(candidate,.35f))continue;
        p.position=candidate;law.recoverStart(uint32_t(i+1),old,candidate);
    }
}
void Game::updateLaw(float dt){
    synchronizeLaw();
    std::array<uint64_t,LawMaxUnits> observerVehicles{};
    for(uint32_t i=0;i<law.state().count;++i)observerVehicles[i]=lawBindings[i].vehicle;
    GameLawSpace space(*this,{observerVehicles.data(),law.state().count});
    for(auto& flash:lawFlashes)flash.remaining=std::max(0.0f,flash.remaining-dt);
    std::array<LawActor,LawMaxUnits> actors{};uint32_t count=0;
    for(uint32_t i=0;i<law.state().count;++i){
        const auto& binding=lawBindings[i];LawActor actor;actor.identity=binding.logical;actor.available=false;actor.alive=false;
        if(binding.pedestrian)for(const auto& p:pedestrians)if(p.identity==binding.pedestrian){actor.position=p.position;actor.yaw=p.yaw;actor.alive=p.health>0;actor.available=world.collisionReady(p.position);break;}
        if(binding.vehicle)for(size_t j=0;j<vehicles.size();++j)if(vehicles[j].identity==binding.vehicle){const auto& v=vehicles[j];actor.position=v.position;actor.yaw=v.yaw;actor.alive=v.health>0;actor.available=int(j)!=occupied&&world.collisionReady(v.position);break;}
        actors[count++]=actor;
    }
    Vec3 observedVelocity=lawHasPreviousPlayer?(player-lawPreviousPlayer)/dt:Vec3{};
    if(length(observedVelocity)>120)observedVelocity={};
    lawPreviousPlayer=player;lawHasPreviousPlayer=true;
    if(wanted>0&&count){
        const Vec3 point=player+Vec3{0,occupied>=0?1.2f:1.1f,0};
        for(uint32_t checked=0;checked<std::min(count,4u);++checked){
            const uint32_t i=lawObserverCursor%count;lawObserverCursor=(lawObserverCursor+1)%count;
            const auto& actor=actors[i];if(!actor.alive||!actor.available)continue;
            const Vec3 eye=actor.position+Vec3{0,1.6f,0},offset=point-eye,horizontal{offset.x,0,offset.z};
            const float range=law.state().units[i].kind==LawUnitKind::Officer?85.0f:95.0f;
            if(length(offset)>range||(length(horizontal)>=3&&dot(normalized(horizontal),forward(actor.yaw))<.422618f))continue;
            space.setObserver(actor.identity);
            if(!space.lineClear(eye,point))continue;
            if(lawEvidenceSerial==std::numeric_limits<uint32_t>::max())break;
            LawEvidence observation;observation.serial=++lawEvidenceSerial;observation.observer=actor.identity;observation.kind=LawEvidenceKind::Sight;observation.position=point;observation.velocity=observedVelocity;
            law.report(observation);
        }
    }
    const int before=wanted;law.update({dt,false,{actors.data(),count}},space);wanted=law.state().wanted;wantedTimer=law.state().alertRemaining;
    if(before>0&&wanted==0){message="The search has ended. You are clear.";messageTime=5;}
    // Resolve at the same physical snapshot used for muzzle clearance, before
    // applying this frame's officer motion. Each ray stops at the first object.
    for(const auto& shot:law.shots()){
        float travel=shot.range;int victim=-1,vehicle=-1;bool playerHit=false;
        for(const auto& chunk:world.chunks)for(const auto& box:chunk.solids)travel=std::min(travel,hitBox(shot.origin,shot.direction,box.min,box.max,travel));
        for(float distance=0;distance<travel;distance+=.5f){const Vec3 p=shot.origin+shot.direction*distance;if(p.y<world.height(p.x,p.z)){travel=distance;break;}}
        for(size_t i=0;i<pedestrians.size();++i){
            const auto& p=pedestrians[i];if(p.health<=0)continue;bool shooter=false;
            for(uint32_t n=0;n<count;++n)if(lawBindings[n].logical==shot.shooter&&lawBindings[n].pedestrian==p.identity)shooter=true;
            if(shooter)continue;
            const float hit=hitPerson(shot.origin,shot.direction,p,travel);
            if(hit<travel){travel=hit;victim=int(i);vehicle=-1;playerHit=false;}
        }
        if(occupied<0){Pedestrian target;target.position=player;const float hit=hitPerson(shot.origin,shot.direction,target,travel);if(hit<travel){travel=hit;victim=-1;vehicle=-1;playerHit=true;}}
        for(size_t i=0;i<vehicles.size();++i){const float hit=hitVehicle(shot.origin,shot.direction,vehicles[i],travel);if(hit<travel){travel=hit;victim=-1;vehicle=int(i);playerHit=false;}}
        if(playerHit&&invulnerabilityTimer<=0)health=std::max(0.0f,health-shot.damage);
        if(victim>=0){auto& p=pedestrians[size_t(victim)];p.health=std::max(0.0f,p.health-shot.damage);frightenPedestrian(size_t(victim),shot.origin,12);}
        if(vehicle>=0)vehicles[size_t(vehicle)].health=std::max(0.0f,vehicles[size_t(vehicle)].health-shot.damage*.8f);
        for(uint32_t i=0;i<count;++i)if(lawBindings[i].logical==shot.shooter)lawFlashes[i]={shot.shooter,.075f,shot.origin,shot.origin+shot.direction*travel};
        alertPedestrians(shot.origin,80);
    }
    for(uint32_t unit=0;unit<count;++unit){
        if(!lawBindings[unit].pedestrian)continue;
        const auto& command=law.commands()[unit];
        for(auto& p:pedestrians)if(p.identity==lawBindings[unit].pedestrian&&p.health>0){
            p.panic=std::max(0.0f,p.panic-dt);const Vec3 beforePosition=p.position;
            Vec3 direction=command.moveTarget-p.position;direction.y=0;const float distance=length(direction);
            if(command.speed>0&&distance>.015f&&world.collisionReady(p.position)){
                Vec3 delta=direction*(std::min(distance,command.speed*dt)/distance);
                // Do not move through the front rank while sharing its sighting.
                // Existing overlaps may separate, but never deepen or swap order.
                for(const auto& other:pedestrians){
                    if(other.identity==p.identity||other.health<=0||std::abs(other.position.y-p.position.y)>1.6f)continue;
                    const float before=planar(p.position,other.position),after=planar(p.position+delta,other.position);
                    if(after<.72f&&after<before){delta={};break;}
                }
                if(planar(p.position+delta,player)<.65f&&planar(p.position+delta,player)<planar(p.position,player))delta={};
                const float travel=length(delta);
                if(travel>.001f)for(const auto& vehicle:vehicles)if(hitVehicle(p.position,delta/travel,vehicle,travel,.35f,true)<travel-.001f){delta={};break;}
                p.position=world.move(p.position,delta,.35f);p.position.y=world.height(p.position.x,p.position.z);
            }
            Vec3 look=command.hasLookTarget?command.lookTarget-p.position:direction;look.y=0;
            if(length(look)>.01f&&(command.speed>0||command.hasLookTarget))p.yaw=wrapAngle(p.yaw+clamp(wrapAngle(std::atan2(look.x,look.z)-p.yaw),-dt*5,dt*5));
            const float moved=planar(p.position,beforePosition);p.phase+=moved*3;p.motion=std::min(1.0f,moved/dt/2.2f);p.activity=p.motion>.05f?PedestrianActivity::Walk:PedestrianActivity::Wait;p.activityTime+=dt;
            break;
        }
    }
}
}
