#include "law.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mc {
namespace {
constexpr float MemoryLifetime=45,FreshSight=.20f,RadioDelay=.30f;
bool number(float n,float lo,float hi){return std::isfinite(n)&&n>=lo&&n<=hi;}
bool point(Vec3 p){return number(p.x,-6144,6144)&&number(p.y,-2048,2048)&&number(p.z,-6144,6144);}
bool velocity(Vec3 p){return number(p.x,-200,200)&&number(p.y,-200,200)&&number(p.z,-200,200);}
bool kindValid(LawEvidenceKind kind){return unsigned(kind)<=unsigned(LawEvidenceKind::Report);}
float distanceXZ(Vec3 a,Vec3 b){const Vec3 d=a-b;return std::sqrt(d.x*d.x+d.z*d.z);}
void ageMemory(LawMemory& memory,float dt){if(memory.valid)memory.age=std::min(600.0f,memory.age+dt);}
bool fresh(const LawMemory& memory){return memory.valid&&memory.age<MemoryLifetime;}
bool contact(const LawUnitState& unit){return unit.memory.valid&&unit.memory.kind==LawEvidenceKind::Sight&&unit.memory.age<=FreshSight&&unit.sightExposure>=.20f;}
bool better(const LawMemory& candidate,const LawMemory& old) {
    if(!fresh(candidate))return false;
    if(!fresh(old))return true;
    if(candidate.kind!=LawEvidenceKind::Sight&&old.kind==LawEvidenceKind::Sight&&old.age<.5f)return false;
    if(candidate.age>old.age+.001f)return false;
    return candidate.age<old.age-.001f||candidate.serial>old.serial;
}
LawMemory memoryOf(const LawEvidence& e){return {true,e.kind,e.serial,e.observer,e.position,e.velocity,e.age,e.uncertainty};}
void releaseSearch(LawUnitState& unit){unit.searchSlot=-1;unit.scanTime=0;}
bool validMemory(const LawMemory& memory,const LawState& state) {
    if(!kindValid(memory.kind)||!point(memory.position)||!velocity(memory.velocity)||!number(memory.age,0,600)||!number(memory.uncertainty,0,128))return false;
    if(!memory.valid)return true;
    if(!memory.serial||memory.serial>state.lastEvidenceSerial)return false;
    if(memory.observer) {
        bool found=false;for(uint32_t i=0;i<state.count;++i)if(state.units[i].identity==memory.observer)found=true;
        if(!found)return false;
    }
    return memory.kind!=LawEvidenceKind::Sight||memory.observer!=0;
}
float movementSpeed(LawPhase phase) {
    switch(phase) {
        case LawPhase::Investigate:return 2.2f;
        case LawPhase::Pursue:return 3.7f;
        case LawPhase::Search:return 1.8f;
        case LawPhase::Return:return 1.3f;
        default:return 0;
    }
}
}

int LawSystem::index(uint32_t identity)const {
    for(uint32_t i=0;i<state_.count;++i)if(state_.units[i].identity==identity)return int(i);
    return -1;
}
void LawSystem::clearTransient() {
    eventCount_=shotCount_=0;stats_={};actors_={};present_={};commands_={};routes_={};checkedListeners_={};navigation_.clear();navigationOwner_=0;navigationGoal_={};
    for(uint32_t i=0;i<state_.count;++i){commands_[i].identity=state_.units[i].identity;commands_[i].phase=state_.units[i].phase;}
}
bool LawSystem::valid(const LawState& state) {
    if(state.schema!=1||state.count>LawMaxUnits||state.wanted>5||!number(state.alertRemaining,0,600)||state.searchedSlots>0xffffu)return false;
    if(state.decisionCursor>=std::max(1u,state.count)||state.weaponCursor>=std::max(1u,state.count))return false;
    if(!validMemory(state.shared,state))return false;
    uint32_t occupied=0;
    for(uint32_t i=0;i<state.count;++i) {
        const auto& unit=state.units[i];const auto& weapon=unit.weapon;
        if(!unit.identity||unsigned(unit.kind)>unsigned(LawUnitKind::Patrol)||unsigned(unit.phase)>unsigned(LawPhase::Down)||!point(unit.home)||!point(unit.goal))return false;
        for(uint32_t j=0;j<i;++j)if(state.units[j].identity==unit.identity)return false;
        if(!validMemory(unit.memory,state)||!validMemory(unit.radio,state)||!number(unit.sightExposure,0,1)||!number(unit.relayDelay,0,RadioDelay+.001f)||!number(unit.decisionDelay,0,1)||!number(unit.scanTime,0,3.5f)||!number(unit.pathRetry,0,5))return false;
        if((unit.memory.valid&&unit.memory.observer!=unit.identity)||(unit.radio.valid&&unit.radio.observer!=unit.identity))return false;
        if(unit.searchSlot< -1||unit.searchSlot>=int(LawSearchSlots)||unit.searchCursor>=LawSearchSlots||unit.searchSerial>state.lastEvidenceSerial)return false;
        if(unit.searchSlot>=0) {
            const uint32_t bit=1u<<unsigned(unit.searchSlot);
            if((occupied&bit)||unit.phase!=LawPhase::Search||!unit.hasGoal)return false;
            occupied|=bit;
        }
        if(weapon.rounds>6||!number(weapon.aimTime,0,AimSeconds+.001f)||!number(weapon.cooldown,0,ShotInterval+.001f)||!number(weapon.reload,0,ReloadSeconds+.001f)||weapon.shotsFired==std::numeric_limits<uint32_t>::max())return false;
        if(weapon.reload>0&&weapon.rounds!=0)return false;
        if(unit.kind==LawUnitKind::Officer&&weapon.rounds==0&&weapon.reload<=0)return false;
        if(weapon.aimTime>0&&(!state.wanted||!unit.memory.valid||unit.memory.kind!=LawEvidenceKind::Sight||weapon.reload>0))return false;
        if(unit.kind==LawUnitKind::Patrol&&(weapon.rounds||weapon.aimTime||weapon.cooldown||weapon.reload||weapon.shotsFired))return false;
        if(unit.phase==LawPhase::Down&&(unit.hasGoal||unit.searchSlot!=-1||weapon.aimTime>0))return false;
    }
    return true;
}
bool LawSystem::reset(std::span<const LawUnitInit> roster) {
    if(roster.size()>LawMaxUnits)return false;
    LawState next;next.count=uint32_t(roster.size());
    for(size_t i=0;i<roster.size();++i) {
        auto& unit=next.units[i];unit.identity=roster[i].identity;unit.kind=roster[i].kind;unit.home=unit.goal=roster[i].home;
        if(unit.kind==LawUnitKind::Patrol)unit.weapon.rounds=0;
    }
    return restore(next);
}
bool LawSystem::restore(const LawState& state) {
    if(!valid(state))return false;
    state_=state;clearTransient();return true;
}
bool LawSystem::alert(uint8_t level,float seconds) {
    if(!level||level>5||!number(seconds,0,600))return false;
    if(!state_.wanted) {
        state_.shared={};state_.searchedSlots=0;
        for(uint32_t i=0;i<state_.count;++i) {
            auto& unit=state_.units[i];unit.memory={};unit.radio={};unit.sightExposure=0;
            unit.hasGoal=false;releaseSearch(unit);unit.weapon.aimTime=0;routes_[i]={};
        }
    }
    state_.wanted=std::max(state_.wanted,level);state_.alertRemaining=std::max(state_.alertRemaining,seconds);return true;
}
bool LawSystem::report(const LawEvidence& e) {
    if(!e.serial||e.serial<=state_.lastEvidenceSerial||!kindValid(e.kind)||!point(e.position)||!velocity(e.velocity)||!number(e.age,0,600)||!number(e.uncertainty,0,128)||e.severity>5||!number(e.alertSeconds,0,600))return false;
    if(e.observer&&index(e.observer)<0)return false;
    if(e.kind==LawEvidenceKind::Sight&&!e.observer)return false;
    if(eventCount_==LawMaxEvents)return false;
    for(uint32_t i=0;i<eventCount_;++i)if(events_[i].serial==e.serial)return false;
    uint32_t position=eventCount_;
    while(position&&events_[position-1].serial>e.serial){events_[position]=events_[position-1];checkedListeners_[position]=checkedListeners_[position-1];--position;}
    events_[position]=e;checkedListeners_[position]=0;++eventCount_;return true;
}
void LawSystem::processEvidence(const LawSpace& space) {
    uint32_t consumed=0;
    while(consumed<eventCount_&&stats_.evidenceProcessed<8) {
        const auto e=events_[consumed];
        if(e.kind==LawEvidenceKind::Sight&&stats_.sightChecks>=4)break;
        int observer=index(e.observer);bool accepted=e.age<MemoryLifetime,deferred=false;
        if(accepted&&e.kind==LawEvidenceKind::Gunshot) {
            accepted=false;
            for(uint32_t attempt=0;attempt<state_.count;++attempt) {
                int listener=-1;float nearest=100.001f;
                for(uint32_t i=0;i<state_.count;++i) {
                    if((checkedListeners_[consumed]&(1u<<i))||!present_[i]||!actors_[i].alive||!actors_[i].available||(e.observer&&state_.units[i].identity!=e.observer))continue;
                    const float distance=length(e.position-(actors_[i].position+Vec3{0,1.6f,0}));
                    if(distance<nearest){nearest=distance;listener=int(i);}
                }
                if(listener<0)break;
                if(nearest>35&&stats_.sightChecks>=4){deferred=true;break;}
                checkedListeners_[consumed]|=1u<<unsigned(listener);
                const Vec3 ear=actors_[size_t(listener)].position+Vec3{0,1.6f,0};
                if(!space.ready(ear)||!space.ready(e.position))continue;
                bool heard=nearest<=35;
                if(!heard){++stats_.sightChecks;heard=space.lineClear(ear,e.position);}
                if(heard){observer=listener;accepted=true;break;}
            }
        }
        if(deferred)break;
        ++consumed;++stats_.evidenceProcessed;state_.lastEvidenceSerial=e.serial;
        if(!accepted)continue;
        if(e.kind==LawEvidenceKind::Sight) {
            if(e.age>FreshSight||observer<0||!present_[size_t(observer)]||!actors_[size_t(observer)].alive||!actors_[size_t(observer)].available)continue;
            const auto& actor=actors_[size_t(observer)];auto& unit=state_.units[size_t(observer)];
            const Vec3 eye=actor.position+Vec3{0,1.6f,0};const float range=unit.kind==LawUnitKind::Patrol?95.0f:85.0f;
            const Vec3 offset=e.position-eye;const Vec3 horizontal{offset.x,0,offset.z};
            const bool inView=length(horizontal)<3||dot(normalized(horizontal),forward(actor.yaw))>=.422618f;
            ++stats_.sightChecks;
            if(!inView||length(offset)>range||!space.ready(eye)||!space.ready(e.position)||!space.lineClear(eye,e.position)) {
                unit.sightExposure=0;unit.weapon.aimTime=0;continue;
            }
        }
        if(e.severity)alert(e.severity,std::max(0.0f,e.alertSeconds-e.age));
        if(!state_.wanted)continue;
        LawMemory incoming=memoryOf(e);
        if(e.kind!=LawEvidenceKind::Report) {
            incoming.observer=state_.units[size_t(observer)].identity;
            auto& unit=state_.units[size_t(observer)];
            if(better(incoming,unit.memory))unit.memory=incoming;
            if(!unit.radio.valid)unit.relayDelay=RadioDelay;
            if(better(incoming,unit.radio))unit.radio=incoming;
        } else if(better(incoming,state_.shared)) {
            state_.shared=incoming;state_.searchedSlots=0;
        }
    }
    for(uint32_t i=consumed;i<eventCount_;++i){events_[i-consumed]=events_[i];checkedListeners_[i-consumed]=checkedListeners_[i];}
    eventCount_-=consumed;
}

bool LawSystem::searchGoal(size_t index,const LawMemory& memory,const LawSpace& space) {
    auto& unit=state_.units[index];
    if(state_.searchedSlots==0xffffu)state_.searchedSlots=0;
    for(unsigned attempt=0;attempt<4&&stats_.projectionChecks<8;++attempt) {
        const uint32_t slot=(unit.identity+unit.searchCursor)%uint32_t(LawSearchSlots);
        unit.searchCursor=(unit.searchCursor+1)%uint32_t(LawSearchSlots);
        if(state_.searchedSlots&(1u<<slot))continue;
        bool used=false;for(uint32_t other=0;other<state_.count;++other)if(other!=index&&present_[other]&&actors_[other].alive&&actors_[other].available&&state_.units[other].searchSlot==int(slot))used=true;
        if(used)continue;
        const float angle=float(slot%4)*Pi*.5f,range=12.0f+float(slot/4)*12.0f+std::min(memory.uncertainty,16.0f);
        const Vec3 requested=memory.position+Vec3{std::sin(angle)*range,0,std::cos(angle)*range};Vec3 goal;
        ++stats_.projectionChecks;
        if(!space.project(requested,unit.kind==LawUnitKind::Officer?.35f:1.05f,goal))continue;
        for(uint32_t other=0;other<state_.count;++other)if(other!=index&&present_[other]&&actors_[other].alive&&actors_[other].available&&state_.units[other].searchSlot>=0&&distanceXZ(state_.units[other].goal,goal)<4)used=true;
        if(used)continue;
        unit.searchSlot=int32_t(slot);unit.goal=goal;unit.hasGoal=true;unit.scanTime=0;return true;
    }
    unit.hasGoal=false;releaseSearch(unit);return false;
}
void LawSystem::moveGoal(size_t index,const LawSpace& space) {
    auto& command=commands_[index];auto& route=routes_[index];auto& unit=state_.units[index];const auto& actor=actors_[index];
    command.speed=0;
    if(!unit.hasGoal||!space.ready(actor.position)||!space.ready(unit.goal))return;
    if(distanceXZ(actor.position,unit.goal)<.65f)return;
    if(unit.kind==LawUnitKind::Patrol) {
        command.moveTarget=unit.goal;command.speed=unit.phase==LawPhase::Pursue?24.0f:9.0f;return;
    }
    if(route.path.count&&distanceXZ(route.destination,unit.goal)>4)route={};
    // Corner clearance is only 12cm beyond the expanded body. A loose arrival
    // radius cuts the next diagonal through the wall before reaching its corner.
    while(route.cursor<route.path.count&&distanceXZ(actor.position,route.path.points[route.cursor])<.04f)++route.cursor;
    const Vec3 next=route.cursor<route.path.count?route.path.points[route.cursor]:unit.goal;
    if(stats_.moveChecks>=2)return;
    ++stats_.moveChecks;
    if(space.walkClear(actor.position,next,.35f)) {
        command.moveTarget=next;command.speed=movementSpeed(unit.phase);return;
    }
    // Revalidate a cached edge too: changed streamed geometry or an adapter's
    // live obstruction must stop movement and start a fresh bounded detour.
    route={};
    if(navigation_.status()!=LawPathStatus::Building&&unit.pathRetry<=0) {
        navigationOwner_=unit.identity;navigationGoal_=unit.goal;
        if(!navigation_.begin(space,actor.position,unit.goal))unit.pathRetry=1;
    }
}
void LawSystem::decide(size_t index,const LawSpace& space) {
    auto& unit=state_.units[index];auto& command=commands_[index];const auto& actor=actors_[index];
    ++stats_.decisions;unit.decisionDelay=.10f;
    command.speed=0;command.aim=false;command.hasLookTarget=false;
    const auto setGoal=[&](Vec3 requested) {
        unit.hasGoal=false;
        if(stats_.projectionChecks>=8)return;
        ++stats_.projectionChecks;
        requested.x=clamp(requested.x,-6144,6144);requested.z=clamp(requested.z,-6144,6144);
        unit.hasGoal=space.project(requested,unit.kind==LawUnitKind::Officer?.35f:1.05f,unit.goal);
    };
    if(!state_.wanted) {
        unit.memory={};unit.radio={};unit.sightExposure=0;releaseSearch(unit);
        unit.hasGoal=distanceXZ(actor.position,unit.home)>.8f;unit.goal=unit.home;
        unit.phase=unit.hasGoal?LawPhase::Return:LawPhase::Patrol;
    } else if(contact(unit)) {
        releaseSearch(unit);unit.searchSerial=0;
        if(unit.kind==LawUnitKind::Officer&&length(unit.memory.position-(actor.position+Vec3{0,1.4f,0}))<=45) {
            unit.phase=LawPhase::Engage;unit.hasGoal=false;
        } else {unit.phase=LawPhase::Pursue;setGoal(unit.memory.position+unit.memory.velocity*std::min(unit.memory.age,.5f));}
    } else {
        const LawMemory* memory=fresh(unit.memory)?&unit.memory:nullptr;
        if(better(state_.shared,memory?*memory:LawMemory{}))memory=&state_.shared;
        if(!memory) {unit.phase=LawPhase::Search;unit.hasGoal=false;if(unit.searchSlot>=0)releaseSearch(unit);}
        else {
            // New observations invalidate a previous sector; a repeated or aged
            // clue never follows a suspect that this system cannot currently see.
            if(unit.searchSerial!=memory->serial||(!unit.hasGoal&&unit.phase!=LawPhase::Search)) {
                unit.searchSerial=memory->serial;releaseSearch(unit);
                setGoal(memory->position+memory->velocity*std::min(memory->age,.5f));
                unit.phase=memory->kind==LawEvidenceKind::Sight?LawPhase::Pursue:LawPhase::Investigate;
            }
            if(unit.phase!=LawPhase::Search&&unit.hasGoal&&distanceXZ(actor.position,unit.goal)<3) {
                unit.phase=LawPhase::Search;unit.hasGoal=false;
            }
            if(unit.phase==LawPhase::Search) {
                if(unit.searchSlot>=0&&unit.scanTime>=3) {
                    state_.searchedSlots|=1u<<unsigned(unit.searchSlot);releaseSearch(unit);unit.hasGoal=false;
                }
                if(!unit.hasGoal)searchGoal(index,*memory,space);
            }
        }
    }
    command.phase=unit.phase;
    if(unit.phase==LawPhase::Search&&(!unit.hasGoal||distanceXZ(actor.position,unit.goal)<1.2f)) {
        const float angle=float(unit.identity%8)*Pi*.25f+(unit.scanTime-1.5f)*.8f;
        command.lookTarget=actor.position+forward(angle)*8+Vec3{0,1.4f,0};command.hasLookTarget=true;
    } else if(fresh(unit.memory)){command.lookTarget=unit.memory.position;command.hasLookTarget=true;}
    else if(unit.hasGoal){command.lookTarget=unit.goal+Vec3{0,1.4f,0};command.hasLookTarget=true;}
    moveGoal(index,space);
}

void LawSystem::advanceWeapons(float dt,const LawSpace& space) {
    for(uint32_t i=0;i<state_.count;++i) {
        auto& unit=state_.units[i];auto& weapon=unit.weapon;auto& command=commands_[i];
        command.aim=false;command.reloadProgress=0;
        if(unit.kind!=LawUnitKind::Officer)continue;
        weapon.cooldown=std::max(0.0f,weapon.cooldown-dt);
        if(weapon.reload>0) {
            weapon.reload=std::max(0.0f,weapon.reload-dt);
            if(weapon.reload<=0)weapon.rounds=6;
            else command.reloadProgress=1-weapon.reload/ReloadSeconds;
        }
        const bool canAim=state_.wanted&&present_[i]&&actors_[i].alive&&actors_[i].available&&contact(unit)&&length(unit.memory.position-(actors_[i].position+Vec3{0,1.4f,0}))<=45;
        if(!canAim||weapon.reload>0)weapon.aimTime=0;
        else {
            weapon.aimTime=std::min(AimSeconds,weapon.aimTime+dt);command.aim=true;
            command.lookTarget=unit.memory.position;command.hasLookTarget=true;
        }
    }
    for(uint32_t offset=0;offset<state_.count;++offset) {
        const uint32_t i=(state_.weaponCursor+offset)%state_.count;auto& unit=state_.units[i];auto& weapon=unit.weapon;
        if(!commands_[i].aim||weapon.aimTime<AimSeconds||weapon.cooldown>1e-5f||weapon.reload>0||!weapon.rounds)continue;
        if(stats_.weaponChecks>=2)break;
        const Vec3 body=actors_[i].position+Vec3{0,1.35f,0};
        const Vec3 aim=unit.memory.position+unit.memory.velocity*std::min(unit.memory.age+.08f,.2f);
        const Vec3 direction=normalized(aim-body),origin=body+direction*.45f;
        ++stats_.weaponChecks;
        // Recheck both shoulder-to-muzzle and muzzle-to-observation. Eye visibility
        // alone must never let a weapon fire through a nearby wall or railing.
        if(!space.lineClear(body,origin)||!space.lineClear(origin,aim)) {weapon.aimTime=0;continue;}
        if(weapon.shotsFired>=std::numeric_limits<uint32_t>::max()-1)continue;
        const uint32_t sequence=++weapon.shotsFired;
        const float spread=(random01(unit.identity^sequence*0x9e3779b9u)-.5f)*.025f;
        const Vec3 shotDirection=normalized(direction+Vec3{spread,spread*.37f,-spread*.31f});
        shots_[shotCount_++]={unit.identity,sequence,origin,shotDirection,45,8};
        --weapon.rounds;weapon.cooldown=ShotInterval;
        if(!weapon.rounds){weapon.reload=ReloadSeconds;weapon.aimTime=0;}
    }
    if(state_.count)state_.weaponCursor=(state_.weaponCursor+1)%state_.count;
}
bool LawSystem::update(const LawFrame& frame,const LawSpace& space) {
    stats_={};shotCount_=0;
    if(frame.actors.size()>LawMaxUnits)return false;
    std::array<bool,LawMaxUnits> supplied{};std::array<LawActor,LawMaxUnits> actors{};
    for(const auto& actor:frame.actors) {
        const int i=index(actor.identity);
        if(i<0||supplied[size_t(i)]||!point(actor.position)||!number(actor.yaw,-10000,10000))return false;
        supplied[size_t(i)]=true;actors[size_t(i)]=actor;
    }
    if(frame.paused||!std::isfinite(frame.dt)||frame.dt<=0) {
        for(uint32_t i=0;i<state_.count;++i){commands_[i].speed=0;commands_[i].aim=false;}
        return true;
    }
    const float dt=std::min(frame.dt,.05f);present_=supplied;actors_=actors;
    ageMemory(state_.shared,dt);
    for(uint32_t i=0;i<eventCount_;++i)events_[i].age=std::min(600.0f,events_[i].age+dt);
    for(uint32_t i=0;i<state_.count;++i) {
        auto& unit=state_.units[i];ageMemory(unit.memory,dt);ageMemory(unit.radio,dt);
        unit.decisionDelay=std::max(0.0f,unit.decisionDelay-dt);unit.pathRetry=std::max(0.0f,unit.pathRetry-dt);
        if(!present_[i]||!actors_[i].available||!space.ready(actors_[i].position)) {
            commands_[i].speed=0;commands_[i].aim=false;releaseSearch(unit);unit.hasGoal=false;unit.sightExposure=0;continue;
        }
        if(!actors_[i].alive) {
            unit.phase=LawPhase::Down;unit.hasGoal=false;releaseSearch(unit);unit.weapon.aimTime=0;unit.sightExposure=0;unit.radio={};routes_[i]={};commands_[i].speed=0;commands_[i].phase=unit.phase;continue;
        }
        if(unit.phase==LawPhase::Down){unit.phase=LawPhase::Patrol;unit.decisionDelay=0;}
        if(unit.phase==LawPhase::Search&&(!unit.hasGoal||distanceXZ(actors_[i].position,unit.goal)<1.2f)) {
            unit.scanTime=std::min(3.5f,unit.scanTime+dt);
            if(unit.searchSlot<0&&unit.scanTime>=3.5f)unit.scanTime=0;
        }
        if(unit.radio.valid) {
            unit.relayDelay=std::max(0.0f,unit.relayDelay-dt);
            if(unit.relayDelay<=0) {
                if(better(unit.radio,state_.shared)){state_.shared=unit.radio;state_.searchedSlots=0;}
                unit.radio={};
            }
        }
    }
    processEvidence(space);
    bool witnessed=false;
    for(uint32_t i=0;i<state_.count;++i) {
        auto& unit=state_.units[i];
        if(present_[i]&&actors_[i].alive&&actors_[i].available&&unit.memory.valid&&unit.memory.kind==LawEvidenceKind::Sight&&unit.memory.age<=FreshSight)
            unit.sightExposure=std::min(1.0f,unit.sightExposure+dt);
        else unit.sightExposure=0;
        if(contact(unit))witnessed=true;
    }
    if(state_.wanted) {
        state_.alertRemaining=std::max(0.0f,state_.alertRemaining-dt);
        if(witnessed)state_.alertRemaining=std::max(state_.alertRemaining,8.0f);
        if(state_.alertRemaining<=0) {
            --state_.wanted;state_.alertRemaining=state_.wanted?12.0f:0.0f;
            if(!state_.wanted){state_.shared={};state_.searchedSlots=0;}
        }
    }
    if(navigation_.status()==LawPathStatus::Building) {
        const int owner=index(navigationOwner_);
        if(owner<0||!present_[size_t(owner)]||!actors_[size_t(owner)].available||!actors_[size_t(owner)].alive||!state_.units[size_t(owner)].hasGoal||distanceXZ(state_.units[size_t(owner)].goal,navigationGoal_)>4)navigation_.clear();
        navigation_.step(space,12);const auto stats=navigation_.stats();
        stats_.navigationChecks=stats.checks;stats_.navigationExpansions=stats.expansions;
        if(owner>=0&&navigation_.status()==LawPathStatus::Ready) {
            routes_[size_t(owner)]={navigation_.path(),0,navigationGoal_};navigation_.clear();
        } else if(owner>=0&&navigation_.status()==LawPathStatus::Failed){state_.units[size_t(owner)].pathRetry=1;navigation_.clear();}
    }
    for(uint32_t checked=0;checked<state_.count&&stats_.decisions<2;++checked) {
        const uint32_t i=state_.decisionCursor;state_.decisionCursor=(state_.decisionCursor+1)%state_.count;
        if(present_[i]&&actors_[i].alive&&actors_[i].available&&state_.units[i].decisionDelay<=0)decide(i,space);
    }
    advanceWeapons(dt,space);return true;
}
}
