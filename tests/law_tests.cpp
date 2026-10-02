#include "../src/law.h"
#include "../src/world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool same(mc::Vec3 a,mc::Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool same(const mc::LawMemory& a,const mc::LawMemory& b){
    return a.valid==b.valid&&a.kind==b.kind&&a.serial==b.serial&&a.observer==b.observer&&
        same(a.position,b.position)&&same(a.velocity,b.velocity)&&a.age==b.age&&a.uncertainty==b.uncertainty;
}
bool same(const mc::LawState& a,const mc::LawState& b){
    if(a.schema!=b.schema||a.count!=b.count||a.lastEvidenceSerial!=b.lastEvidenceSerial||a.wanted!=b.wanted||
       a.alertRemaining!=b.alertRemaining||!same(a.shared,b.shared)||a.searchedSlots!=b.searchedSlots||
       a.decisionCursor!=b.decisionCursor||a.weaponCursor!=b.weaponCursor)return false;
    for(size_t i=0;i<mc::LawMaxUnits;++i){
        const auto& x=a.units[i];const auto& y=b.units[i];
        if(x.identity!=y.identity||x.kind!=y.kind||x.phase!=y.phase||!same(x.home,y.home)||!same(x.goal,y.goal)||
           x.hasGoal!=y.hasGoal||!same(x.memory,y.memory)||!same(x.radio,y.radio)||x.sightExposure!=y.sightExposure||
           x.relayDelay!=y.relayDelay||x.decisionDelay!=y.decisionDelay||x.scanTime!=y.scanTime||x.pathRetry!=y.pathRetry||
           x.searchSlot!=y.searchSlot||x.searchCursor!=y.searchCursor||x.searchSerial!=y.searchSerial||
           x.weapon.rounds!=y.weapon.rounds||x.weapon.aimTime!=y.weapon.aimTime||x.weapon.cooldown!=y.weapon.cooldown||
           x.weapon.reload!=y.weapon.reload||x.weapon.shotsFired!=y.weapon.shotsFired)return false;
    }
    return true;
}
class OpenSpace final:public mc::LawSpace {
public:
    bool available=true,visible=true,lowCover=false;
    float clearAfterX=-std::numeric_limits<float>::infinity();
    bool ready(mc::Vec3)const override{return available;}
    bool project(mc::Vec3 p,float,mc::Vec3& ground)const override{ground={p.x,0,p.z};return available;}
    bool walkClear(mc::Vec3,mc::Vec3,float)const override{return available;}
    bool lineClear(mc::Vec3 from,mc::Vec3)const override{return available&&visible&&from.x>=clearAfterX&&(!lowCover||from.y>=1.5f);}
    size_t obstacles(mc::Vec3,float,std::span<mc::LawObstacle>)const override{return 0;}
};
struct Fixture {
    mc::LawSystem law;
    OpenSpace space;
    std::vector<mc::LawActor> actors;
    uint32_t serial=0;
    explicit Fixture(size_t count=1){
        std::vector<mc::LawUnitInit> units;
        for(size_t i=0;i<count;++i){const mc::Vec3 p{float(i)*4,0,0};units.push_back({uint32_t(i+1),mc::LawUnitKind::Officer,p});actors.push_back({uint32_t(i+1),p,0,true,true});}
        require(law.reset(units),"valid officer roster rejected");
    }
    void step(float dt=1.f/60,bool paused=false){
        require(law.update({dt,paused,actors},space),"valid law frame rejected");
        const auto s=law.stats();
        require(s.evidenceProcessed<=8&&s.sightChecks<=4,"law evidence or sight budget exceeded");
        require(s.decisions<=2&&s.weaponChecks<=2&&s.moveChecks<=2&&s.projectionChecks<=8&&
                s.navigationChecks<=12&&s.navigationExpansions<=16,"law decision, weapon, or navigation budget exceeded");
        require(law.shots().size()<=mc::LawMaxShots,"law shot output exceeded capacity");
        require(mc::LawSystem::valid(law.state()),"simulation produced an invalid persistence state");
    }
    mc::LawEvidence evidence(mc::LawEvidenceKind kind,mc::Vec3 position={0,1.1f,18},uint32_t observer=1){
        mc::LawEvidence e;e.serial=++serial;e.observer=observer;e.kind=kind;e.position=position;e.severity=1;e.alertSeconds=20;return e;
    }
    void sight(float dt=1.f/60){require(law.report(evidence(mc::LawEvidenceKind::Sight)),"fresh sight rejected");step(dt);}
};

void unknownLocationAndObservationMemory(){
    Fixture f;
    require(f.law.alert(2,5),"valid alert rejected");
    for(int frame=0;frame<60;++frame){f.step();require(!f.law.state().shared.valid&&!f.law.state().units[0].memory.valid,"unknown alert invented suspect location");require(f.law.shots().empty(),"unknown alert produced a shot");}
    auto observation=f.evidence(mc::LawEvidenceKind::Sight);observation.velocity={4,0,0};
    require(f.law.report(observation),"moving observation rejected");f.step();
    const mc::Vec3 remembered=f.law.state().units[0].memory.position;
    for(int frame=0;frame<120;++frame){
        f.step();const auto& unit=f.law.state().units[0];
        require(same(unit.memory.position,remembered),"unobserved suspect continued updating remembered position");
        require(f.law.shots().empty(),"stale sight produced a shot");
        if(unit.phase==mc::LawPhase::Pursue||unit.phase==mc::LawPhase::Investigate)
            require(unit.goal.x<=remembered.x+2.01f,"unseen movement prediction exceeded half a second");
    }
    require(f.law.state().units[0].memory.age>1.9f,"unobserved evidence did not age");
}

void hiddenSuspectInvariance(){
    Fixture a(4);
    for(int frame=0;frame<30;++frame)a.sight();
    Fixture b=a;
    mc::Vec3 hiddenA{0,0,18},hiddenB=hiddenA;
    for(int frame=0;frame<300;++frame){
        // The host knows the hidden actor's movement; the law API receives no new observation.
        hiddenA+=mc::Vec3{.2f,0,.1f};hiddenB+=mc::Vec3{-.1f,0,-.2f};
        a.step();b.step();
        require(same(a.law.state(),b.law.state()),"hidden actor position changed police state without evidence");
        require(a.law.shots().size()==b.law.shots().size(),"hidden actor position changed firing without evidence");
        for(size_t i=0;i<a.actors.size();++i){
            const auto& x=a.law.commands()[i];const auto& y=b.law.commands()[i];
            require(x.phase==y.phase&&same(x.moveTarget,y.moveTarget)&&same(x.lookTarget,y.lookTarget)&&
                    x.speed==y.speed&&x.aim==y.aim&&x.hasLookTarget==y.hasLookTarget,
                    "hidden actor position changed a pursuit command without evidence");
            const auto delta=x.moveTarget-a.actors[i].position;const float distance=mc::length(delta);
            if(distance>0&&x.speed>0)a.actors[i].position+=delta*(std::min(distance,x.speed/60)/distance);
            b.actors[i].position=a.actors[i].position;
        }
    }
    require(mc::length(hiddenA-hiddenB)>100,"hidden-suspect scenarios did not diverge");
}
void mixedOfficersAndPatrols(){
    Fixture f;
    const std::array<mc::LawUnitInit,2> units{{{1,mc::LawUnitKind::Officer,{0,0,0}},{2,mc::LawUnitKind::Patrol,{6,0,0}}}};
    require(f.law.reset(units),"mixed law roster rejected");
    f.actors={{1,{0,0,0},0,true,true},{2,{6,0,0},0,true,true}};
    uint32_t officerShots=0;
    for(int frame=0;frame<180;++frame){
        require(f.law.report(f.evidence(mc::LawEvidenceKind::Sight,{0,1.1f,18},1)),"officer sight rejected");
        require(f.law.report(f.evidence(mc::LawEvidenceKind::Sight,{0,1.1f,18},2)),"patrol sight rejected");
        f.step();for(const auto& shot:f.law.shots()){require(shot.shooter==1,"patrol vehicle emitted an on-foot weapon shot");++officerShots;}
    }
    require(officerShots>0&&f.law.state().units[1].weapon.shotsFired==0,"mixed unit weapon roles were not preserved");
    require(f.law.state().units[1].hasGoal,"visible suspect did not produce a patrol pursuit goal");
}
void runWorldDetour(bool insertObstacle){
    mc::World world;
    for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x){mc::Chunk chunk;chunk.x=x;chunk.z=z;world.chunks.push_back(std::move(chunk));}
    world.chunks[24].solids.push_back({{-1,-1,-6},{1,3,6}});
    mc::WorldLawSpace space(world);mc::LawSystem law;
    const std::array<mc::LawUnitInit,1> units{{{1,mc::LawUnitKind::Officer,{-12,0,0}}}};
    std::array<mc::LawActor,1> actors{{{1,{-12,0,0},0,true,true}}};
    require(law.reset(units),"world detour roster rejected");
    mc::LawEvidence clue;clue.serial=1;clue.kind=mc::LawEvidenceKind::Report;clue.position={12,1.1f,0};clue.severity=1;clue.alertSeconds=60;
    require(law.report(clue),"world detour report rejected");
    bool reached=false,around=false,obstructionAdded=!insertObstacle;float travelled=0;
    for(int frame=0;frame<2400&&!reached;++frame){
        require(law.update({1.f/60,false,actors},space),"world detour update rejected");
        require(mc::LawSystem::valid(law.state()),"world detour produced invalid state");
        const auto& command=law.commands()[0];
        if(command.speed>0){
            const auto delta=command.moveTarget-actors[0].position;const float distance=mc::length(delta);
            if(!obstructionAdded&&distance>6){
                const auto center=(command.moveTarget+actors[0].position)*.5f;
                world.chunks[24].solids.push_back({{center.x-.7f,-1,center.z-.7f},{center.x+.7f,3,center.z+.7f}});
                obstructionAdded=true;
            }
            const auto next=actors[0].position+delta*(std::min(distance,command.speed/60)/std::max(distance,.0001f));
            if(!space.walkClear(actors[0].position,next,.35f))
                std::cerr<<"Detour blocked at frame "<<frame<<" from "<<actors[0].position.x<<','<<actors[0].position.y<<','<<actors[0].position.z
                         <<" toward "<<command.moveTarget.x<<','<<command.moveTarget.y<<','<<command.moveTarget.z
                         <<" next "<<next.x<<','<<next.y<<','<<next.z<<" phase "<<int(command.phase)<<'\n';
            require(space.walkClear(actors[0].position,next,.35f),"police movement command crossed collision geometry");
            travelled+=mc::length(next-actors[0].position);actors[0].position=next;
            require(!world.blocked(next,.35f),"officer entered the wall while following its route");
            around|=std::abs(next.z)>6;
        }
        mc::Vec3 offset=actors[0].position-mc::Vec3{12,0,0};offset.y=0;reached=mc::length(offset)<3;
    }
    require(reached&&around&&travelled>24&&obstructionAdded,"command-driven officer did not navigate around current walls to the reported location");
}
void commandDrivenWorldDetour(){runWorldDetour(false);runWorldDetour(true);}

void evidenceReuseAndQueueBounds(){
    Fixture f;
    auto e=f.evidence(mc::LawEvidenceKind::Report,{20,0,30});
    require(f.law.report(e),"valid report rejected");f.step();
    for(int i=0;i<30;++i)f.step();
    const auto before=f.law.state();
    require(!f.law.report(e),"reused evidence serial accepted");f.step();
    require(f.law.state().shared.age>before.shared.age,"reused report refreshed evidence age");
    auto stale=f.evidence(mc::LawEvidenceKind::Report,{-20,0,-30});stale.age=10;
    require(f.law.report(stale),"well-formed old report rejected before prioritization");f.step();
    require(same(f.law.state().shared.position,before.shared.position),"older clue replaced fresher evidence");
    Fixture queue;
    for(size_t i=0;i<mc::LawMaxEvents;++i)require(queue.law.report(queue.evidence(mc::LawEvidenceKind::Report)),"bounded queue filled prematurely");
    require(!queue.law.report(queue.evidence(mc::LawEvidenceKind::Report)),"event queue exceeded capacity");
    for(int i=0;i<8;++i)queue.step();
    require(queue.law.queuedEvidence()==0,"bounded report queue never drained");
    auto invalid=queue.evidence(mc::LawEvidenceKind::Report);invalid.position.x=std::numeric_limits<float>::quiet_NaN();
    require(!queue.law.report(invalid),"NaN clue accepted");
    Fixture ordering;
    auto older=ordering.evidence(mc::LawEvidenceKind::Report,{-10,0,0});
    auto newer=ordering.evidence(mc::LawEvidenceKind::Report,{10,0,0});
    require(ordering.law.report(newer)&&ordering.law.report(older),"out-of-order unconsumed evidence rejected");
    require(!ordering.law.report(older),"duplicate queued evidence accepted");ordering.step();
    require(ordering.law.state().lastEvidenceSerial==newer.serial&&same(ordering.law.state().shared.position,newer.position),
            "out-of-order evidence was not consumed in serial order");
}

void gunshotAudibilityAndDeferredListeners(){
    for(int scenario=0;scenario<4;++scenario){
        Fixture f;
        const float distance=scenario==0?150.f:(scenario==1?10.f:60.f);
        f.space.visible=scenario==0||scenario==3;
        auto sound=f.evidence(mc::LawEvidenceKind::Gunshot,{0,1.1f,distance},0);
        require(f.law.report(sound),"valid sound evidence rejected");
        for(int frame=0;frame<40;++frame)f.step();
        const bool shouldHear=scenario==1||scenario==3;
        require(f.law.state().units[0].memory.valid==shouldHear,"gunshot hearing ignored distance or occlusion");
        require(f.law.state().shared.valid==shouldHear,"inaudible sound leaked into shared police knowledge");
        if(shouldHear)require(f.law.state().units[0].memory.kind==mc::LawEvidenceKind::Gunshot,
                             "hearing was promoted to direct visual contact");
        require(f.law.state().units[0].weapon.shotsFired==0,"officer shot at a sound without visual contact");
    }
    Fixture many(mc::LawMaxUnits);many.space.clearAfterX=48;
    require(many.law.report(many.evidence(mc::LawEvidenceKind::Gunshot,{0,1.1f,60},0)),"multi-listener sound rejected");
    for(int frame=0;frame<30;++frame)many.step();
    require(many.law.state().units[12].memory.valid&&many.law.state().units[12].memory.kind==mc::LawEvidenceKind::Gunshot,
            "bounded hearing scan dropped a farther audible listener after closer occluded listeners");
    require(many.law.state().shared.valid,"deferred audible listener did not relay the sound");
    require(many.law.queuedEvidence()==0,"completed listener scan retained the event forever");
}

void sightRequiresObserverAndGeometry(){
    for(int scenario=0;scenario<5;++scenario){
        Fixture f;
        if(scenario==0)f.actors[0].alive=false;
        if(scenario==1)f.actors[0].available=false;
        if(scenario==2)f.space.visible=false;
        if(scenario==3)f.space.available=false;
        if(scenario==4)f.actors[0].yaw=mc::Pi;
        for(int i=0;i<90;++i)f.sight();
        require(!f.law.state().shared.valid&&!f.law.state().units[0].memory.valid,"invalid observer or blocked geometry supplied sight");
        require(f.law.state().units[0].weapon.shotsFired==0,"invalid sight enabled a shot");
    }
    Fixture f;
    for(int i=0;i<8;++i){f.sight();require(f.law.state().units[0].phase!=mc::LawPhase::Engage,"engagement bypassed exposure requirement");}
    for(int i=0;i<30;++i)f.sight();
    require(f.law.state().units[0].phase==mc::LawPhase::Engage,"sustained visible exposure never engaged");
}

void radioAndSearchAssignments(){
    Fixture f(4);
    require(f.law.report(f.evidence(mc::LawEvidenceKind::Sight,{0,1.1f,4})),"sight report rejected");f.step();
    require(!f.law.state().units[1].memory.valid,"radio relay had no delay");
    for(int i=0;i<30;++i)f.step();
    require(f.law.state().shared.valid,"observation never reached shared radio memory");
    for(size_t i=1;i<4;++i){
        require(!f.law.state().units[i].memory.valid,"radio report was incorrectly treated as direct sight");
        require(f.law.state().units[i].hasGoal,"radio report never produced an investigation goal");
        require(f.law.state().units[i].phase!=mc::LawPhase::Engage&&f.law.state().units[i].weapon.shotsFired==0,
                "radio-only recipient engaged without its own visual observation");
    }
    std::array<bool,4> searched{};
    for(int frame=0;frame<600;++frame){
        f.step();std::array<bool,mc::LawSearchSlots> occupied{};
        for(size_t i=0;i<4;++i){
            const auto& unit=f.law.state().units[i];
            if(unit.searchSlot>=0){require(!occupied[size_t(unit.searchSlot)],"officers reserved the same search assignment");occupied[size_t(unit.searchSlot)]=true;searched[i]=true;}
            const auto& command=f.law.commands()[i];const auto delta=command.moveTarget-f.actors[i].position;
            const float distance=mc::length(delta);
            if(distance>0&&command.speed>0)f.actors[i].position+=delta*(std::min(distance,command.speed/60)/distance);
        }
    }
    require(std::all_of(searched.begin(),searched.end(),[](bool value){return value;}),"some officers never received a search assignment");
    f.actors[0].alive=false;f.step();
    require(f.law.state().units[0].searchSlot==-1,"dead officer retained its search assignment");
}

struct FireResult {uint32_t shots=0;std::vector<float> times;};
FireResult runFiring(float dt){
    Fixture f;FireResult result;
    for(int frame=0;frame<int(std::lround(12/dt));++frame){
        f.sight(dt);
        for(const auto& shot:f.law.shots()){
            require(shot.shooter==1&&shot.sequence==result.shots+1,"shot identity or sequence invalid");
            require(std::abs(mc::length(shot.direction)-1)<.001f&&shot.range>0&&shot.damage>0,"invalid shot geometry or damage");
            result.times.push_back(float(frame+1)*dt);++result.shots;
        }
    }
    require(result.shots>=7,"officer did not fire and reload during sustained sight");
    require(result.times.front()>=mc::LawSystem::AimSeconds-.04f,"officer fired before aiming");
    for(size_t i=1;i<result.times.size();++i)require(result.times[i]-result.times[i-1]>=mc::LawSystem::ShotInterval-.04f,"weapon fired faster than cadence");
    require(result.times[6]-result.times[5]>=mc::LawSystem::ReloadSeconds-.04f,"seventh shot bypassed magazine reload");
    return result;
}
void discreteCadenceAndReload(){const auto a=runFiring(1.f/60),b=runFiring(1.f/30);require(std::abs(int(a.shots)-int(b.shots))<=1,"weapon cadence depends on frame rate");}

void coverInterruptsAimAndDeadUnits(){
    Fixture f;
    for(int i=0;i<35;++i)f.sight();
    require(f.law.state().units[0].weapon.shotsFired==0,"aim fixture fired too early");
    f.space.visible=false;
    for(int i=0;i<120;++i){f.sight();require(f.law.shots().empty(),"officer fired through newly inserted cover");}
    f.space.visible=true;
    for(int i=0;i<120;++i)f.sight();
    require(f.law.state().units[0].weapon.shotsFired>0,"officer never resumed after clear reacquisition");
    f.actors[0].alive=false;
    for(int i=0;i<90;++i){f.sight();require(f.law.shots().empty(),"dead officer emitted shots");}
    Fixture low;low.space.lowCover=true;
    for(int i=0;i<150;++i)low.sight();
    require(low.law.state().units[0].phase==mc::LawPhase::Engage&&low.law.state().units[0].weapon.shotsFired==0,
            "eye-visible target allowed shots through muzzle-height cover");
    low.space.lowCover=false;for(int i=0;i<90;++i)low.sight();
    require(low.law.state().units[0].weapon.shotsFired>0,"weapon did not resume after muzzle clearance");
}

void pauseAndMalformedFrames(){
    Fixture f;
    for(int i=0;i<120;++i){f.sight();if(!f.law.shots().empty())break;}
    require(f.law.state().units[0].weapon.shotsFired>0,"pause fixture never fired");
    auto frozen=f.law.state();
    require(f.law.report(f.evidence(mc::LawEvidenceKind::Report)),"pause fixture report rejected");
    const size_t queued=f.law.queuedEvidence();
    f.step(1.f/60,true);require(same(frozen,f.law.state())&&f.law.shots().empty(),"pause advanced state or replayed a shot");
    require(f.law.queuedEvidence()==queued,"pause discarded a pending observation");
    for(float dt:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        f.step(dt);require(same(frozen,f.law.state())&&f.law.shots().empty(),"invalid dt advanced law state");
    }
    auto malformed=f.actors;malformed[0].position.x=std::numeric_limits<float>::quiet_NaN();
    require(!f.law.update({1.f/60,false,malformed},f.space),"malformed actor accepted");
    require(same(frozen,f.law.state()),"malformed actor partially advanced persistent state");
}

void persistenceValidationAndContinuation(){
    Fixture f;
    for(int i=0;i<150;++i)f.sight();
    const auto snapshot=f.law.state();mc::LawSystem restored;
    require(restored.restore(snapshot),"valid active law state rejected");
    auto reject=[&](mc::LawState bad){require(!mc::LawSystem::valid(bad)&&!restored.restore(bad),"invalid law state accepted");require(same(restored.state(),snapshot),"failed restore partially mutated law state");};
    auto bad=snapshot;bad.schema=2;reject(bad);bad=snapshot;bad.count=mc::LawMaxUnits+1;reject(bad);
    bad=snapshot;bad.wanted=6;reject(bad);bad=snapshot;bad.alertRemaining=-1;reject(bad);
    bad=snapshot;bad.units[0].identity=0;reject(bad);bad=snapshot;bad.units[0].phase=mc::LawPhase(255);reject(bad);
    bad=snapshot;bad.units[0].kind=mc::LawUnitKind(255);reject(bad);
    bad=snapshot;bad.units[0].home.x=std::numeric_limits<float>::quiet_NaN();reject(bad);
    bad=snapshot;bad.units[0].memory.age=-1;reject(bad);bad=snapshot;bad.units[0].memory.uncertainty=-1;reject(bad);
    bad=snapshot;bad.units[0].memory.kind=mc::LawEvidenceKind(255);reject(bad);
    bad=snapshot;bad.units[0].memory.velocity.z=std::numeric_limits<float>::quiet_NaN();reject(bad);
    bad=snapshot;bad.units[0].goal.x=1e9f;reject(bad);
    bad=snapshot;bad.units[0].weapon.rounds=7;reject(bad);bad=snapshot;bad.units[0].weapon.cooldown=-1;reject(bad);
    bad=snapshot;bad.units[0].weapon.reload=std::numeric_limits<float>::infinity();reject(bad);
    bad=snapshot;bad.units[0].searchSlot=int32_t(mc::LawSearchSlots);reject(bad);
    bad=snapshot;bad.count=2;bad.units[1]=bad.units[0];reject(bad);
    bad=snapshot;bad.units[0].memory.observer=0;reject(bad);
    bad=snapshot;bad.units[0].weapon.rounds=0;bad.units[0].weapon.reload=0;reject(bad);
    bad=snapshot;bad.units[0].memory.kind=mc::LawEvidenceKind::Report;bad.units[0].weapon.aimTime=.3f;reject(bad);
    for(int i=0;i<180;++i){
        const auto e=f.evidence(mc::LawEvidenceKind::Sight);require(f.law.report(e)&&restored.report(e),"continuation sight rejected");f.step();
        require(restored.update({1.f/60,false,f.actors},f.space),"restored update rejected");
        require(same(f.law.state(),restored.state()),"restored law state diverged during active engagement");
        require(f.law.shots().size()==restored.shots().size(),"restore changed firing timing");
    }
}
}
int main(){
    struct Test{const char* name;void(*run)();};
    const Test tests[]={{"unknown location and bounded observed memory",unknownLocationAndObservationMemory},{"hidden suspect invariance",hiddenSuspectInvariance},{"mixed officer and patrol weapon roles",mixedOfficersAndPatrols},{"command-driven real-world detour",commandDrivenWorldDetour},{"evidence reuse and bounded queue",evidenceReuseAndQueueBounds},{"gunshot audibility and deferred listeners",gunshotAudibilityAndDeferredListeners},{"sight requires observer and geometry",sightRequiresObserverAndGeometry},{"radio delay and unique search assignments",radioAndSearchAssignments},{"discrete cadence and magazine reload",discreteCadenceAndReload},{"cover interrupts aim and dead units",coverInterruptsAimAndDeadUnits},{"pause and malformed frames",pauseAndMalformedFrames},{"state validation and continuation",persistenceValidationAndContinuation}};
    int failures=0;for(const auto& test:tests){try{test.run();std::cout<<"PASS "<<test.name<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<'\n';}}
    std::cout<<std::size(tests)-failures<<'/'<<std::size(tests)<<" law tests passed\n";return failures?1:0;
}
