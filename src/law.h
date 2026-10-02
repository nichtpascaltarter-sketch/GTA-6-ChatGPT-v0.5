#pragma once
#include "law_navigation.h"
#include <array>
#include <cstdint>
#include <span>

namespace mc {
constexpr size_t LawMaxUnits=16,LawSearchSlots=16,LawMaxEvents=32,LawMaxShots=16;
enum class LawUnitKind:uint8_t {Officer,Patrol};
enum class LawPhase:uint8_t {Patrol,Investigate,Pursue,Search,Engage,Return,Down};
enum class LawEvidenceKind:uint8_t {Sight,Gunshot,Report};

struct LawUnitInit {uint32_t identity=0;LawUnitKind kind=LawUnitKind::Officer;Vec3 home;};
struct LawActor {uint32_t identity=0;Vec3 position;float yaw=0;bool alive=true,available=true;};
struct LawEvidence {
    uint32_t serial=0,observer=0;
    LawEvidenceKind kind=LawEvidenceKind::Report;
    // Observed body point or actual sound/report origin, never a live target reference.
    // Sight requires an observer. Gunshot with observer zero tests all listeners;
    // Report is an already validated dispatch report rather than a sound query.
    Vec3 position,velocity;
    float age=0,uncertainty=0;
    uint8_t severity=0;
    float alertSeconds=0;
};
struct LawMemory {
    bool valid=false;
    LawEvidenceKind kind=LawEvidenceKind::Report;
    uint32_t serial=0,observer=0;
    Vec3 position,velocity;
    float age=0,uncertainty=0;
};
struct LawWeapon {
    uint8_t rounds=6;
    float aimTime=0,cooldown=0,reload=0;
    uint32_t shotsFired=0;
};
struct LawUnitState {
    uint32_t identity=0;
    LawUnitKind kind=LawUnitKind::Officer;
    LawPhase phase=LawPhase::Patrol;
    Vec3 home,goal;
    bool hasGoal=false;
    LawMemory memory,radio;
    float sightExposure=0,relayDelay=0,decisionDelay=0,scanTime=0,pathRetry=0;
    int32_t searchSlot=-1;
    uint32_t searchCursor=0,searchSerial=0;
    LawWeapon weapon;
};

// Value-only persistence payload. Serialize fields explicitly, never raw object bytes.
// Paths, current actors, queued observations and presentation events are reconstructed.
struct LawState {
    uint32_t schema=1,count=0,lastEvidenceSerial=0;
    uint8_t wanted=0;
    float alertRemaining=0;
    LawMemory shared;
    uint32_t searchedSlots=0,decisionCursor=0,weaponCursor=0;
    std::array<LawUnitState,LawMaxUnits> units{};
};
struct LawFrame {
    float dt=0;
    bool paused=false;
    std::span<const LawActor> actors;
};
struct LawCommand {
    uint32_t identity=0;
    LawPhase phase=LawPhase::Patrol;
    Vec3 moveTarget,lookTarget;
    float speed=0,reloadProgress=0;
    bool hasLookTarget=false,aim=false;
};
struct LawShot {
    uint32_t shooter=0,sequence=0;
    Vec3 origin,direction;
    float range=45,damage=8;
};
struct LawStats {
    uint32_t decisions=0,evidenceProcessed=0,sightChecks=0,weaponChecks=0;
    uint32_t moveChecks=0,projectionChecks=0,navigationChecks=0,navigationExpansions=0;
};

class LawSystem {
public:
    static constexpr float AimSeconds=.75f,ShotInterval=.70f,ReloadSeconds=2.4f;
    bool reset(std::span<const LawUnitInit>);
    bool restore(const LawState&);
    static bool valid(const LawState&);
    bool alert(uint8_t level,float seconds);
    bool report(const LawEvidence&);
    // Returns false for malformed actor inputs. Pause/invalid dt never advances state.
    bool update(const LawFrame&,const LawSpace&);
    const LawState& state() const{return state_;}
    std::span<const LawCommand> commands() const{return {commands_.data(),state_.count};}
    std::span<const LawShot> shots() const{return {shots_.data(),shotCount_};}
    LawStats stats() const{return stats_;}
    size_t queuedEvidence() const{return eventCount_;}
private:
    struct Route {LawPath path;uint32_t cursor=0;Vec3 destination;};
    int index(uint32_t identity) const;
    void processEvidence(const LawSpace&);
    void decide(size_t,const LawSpace&);
    bool searchGoal(size_t,const LawMemory&,const LawSpace&);
    void moveGoal(size_t,const LawSpace&);
    void advanceWeapons(float,const LawSpace&);
    void clearTransient();
    LawState state_{};
    LawStats stats_{};
    std::array<LawActor,LawMaxUnits> actors_{};
    std::array<bool,LawMaxUnits> present_{};
    std::array<LawCommand,LawMaxUnits> commands_{};
    std::array<LawEvidence,LawMaxEvents> events_{};
    std::array<uint32_t,LawMaxEvents> checkedListeners_{};
    std::array<LawShot,LawMaxShots> shots_{};
    std::array<Route,LawMaxUnits> routes_{};
    uint32_t eventCount_=0,shotCount_=0;
    LawNavigation navigation_;
    uint32_t navigationOwner_=0;
    Vec3 navigationGoal_;
};
}
