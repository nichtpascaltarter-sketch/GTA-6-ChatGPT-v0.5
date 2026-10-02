#pragma once
#include "audio.h"
#include "game.h"
#include "world_gunfire_scene.h"
#include <algorithm>
#include <limits>

namespace mc {
inline void movementAudio(AudioState& state,const Game& game,Vec3 previousPosition,
                          int previousVehicle,const Input& input,float dt) {
    state.footSpeed=state.waterMotion=state.tireScrub=0;state.footContact=false;
    if(game.paused||!std::isfinite(dt)||dt<=0)return;
    if(game.occupied>=0) {
        if(size_t(game.occupied)>=game.vehicles.size())return;
        const auto& vehicle=game.vehicles[size_t(game.occupied)];
        if(vehicle.kind==VehicleKind::Car||vehicle.kind==VehicleKind::Motorcycle) {
            const float slip=std::abs(dot(vehicle.velocity,right(vehicle.yaw)));
            state.tireScrub=clamp((slip-1.2f)/7.f+(input.brake?std::abs(vehicle.speed)/35.f:0.f),0,1);
        }
        return;
    }
    if(previousVehicle>=0)return;
    const float speed=std::hypot(game.player.x-previousPosition.x,game.player.z-previousPosition.z)/dt;
    // Respawns and position corrections must not sound like a burst of footsteps.
    if(!std::isfinite(speed)||speed>10)return;
    const float depth=game.world.waterDepth(game.player.x,game.player.z);
    if(depth>.05f&&game.player.y<World::WaterLevel+.25f) {
        state.waterMotion=clamp(speed/3.4f,0,1);return;
    }
    state.footSpeed=speed;
    state.footContact=std::abs(game.player.y-game.world.height(game.player.x,game.player.z))<.06f;
    switch(game.world.groundSurface(game.player.x,game.player.z)) {
    case GroundSurface::Pavement:state.footSurface=FootSurface::Pavement;break;
    case GroundSurface::Soil:state.footSurface=FootSurface::Soil;break;
    case GroundSurface::Grass:state.footSurface=FootSurface::Grass;break;
    case GroundSurface::Sand:state.footSurface=FootSurface::Sand;break;
    case GroundSurface::Wood:state.footSurface=FootSurface::Wood;break;
    }
}

struct WorldAudioSceneStats {
    uint64_t snapshots=0,epochResets=0,sourceAdmissions=0,sourceRenewals=0;
    uint64_t contacts=0,rejectedSamples=0,duplicateIdentities=0,capacityDrops=0;
    unsigned trackedEngines=0,trackedFeet=0;
};

// Simulation-thread tracker, independent of the audio callback. All storage is
// fixed: at most 256 vehicles and 128 people, with eight nearest sources of each
// kind plus all officer shot counters published. No entity index, borrowed
// pointer, or world revision is an audio source ID.
class WorldAudioScene {
public:
    static constexpr unsigned EngineCapacity=256,FootCapacity=128;
    static constexpr float EngineRadius=144,FootRadius=32,GunRadius=220;
    const WorldAudioSceneStats& stats() const noexcept {return statistics;}
    // Explicit resets keep counters but renew the audio epoch. Focus suspension
    // normally uses update(..., 0, false, worldEpoch), preserving engine IDs.
    void reset(uint64_t worldEpoch) noexcept {
        engines[0].clear();engines[1].clear();feet[0].clear();feet[1].clear();
        gunfire.reset();
        previous=0;sourceWorldEpoch=worldEpoch;initialized=true;hasListener=false;
        ++audioEpoch;if(audioEpoch==0)++audioEpoch;
        ++statistics.epochResets;statistics.trackedEngines=statistics.trackedFeet=0;
    }
    // simulationDeltaSeconds is the actual clamped simulation step, or zero if
    // Game::update did not run. Advancing is separate from listener heartbeats.
    // Publishing once with advancing=false breaks contact continuity; the first
    // resumed sample primes again, without changing stationary engine IDs.
    WorldAudioState update(const Game& game,Vec3 listenerPosition,Vec3 listenerForward,
                           float simulationDeltaSeconds,bool advancing,uint64_t worldEpoch) noexcept {
        WorldAudioState result;
        const bool validDelta=std::isfinite(simulationDeltaSeconds)&&simulationDeltaSeconds>0&&simulationDeltaSeconds<=.051f;
        const float dt=validDelta?simulationDeltaSeconds:0;
        if(!initialized||sourceWorldEpoch!=worldEpoch)reset(worldEpoch);
        if(!positionValid(listenerPosition)) {
            reset(worldEpoch);++statistics.rejectedSamples;
            return finish(result,false);
        }
        // This exceeds even aircraft travel during one legal game step. A
        // discontinuous camera cut must not replay sources from the prior view.
        if(hasListener&&distance(listenerPosition,lastListener)>std::max(24.0f,dt*350))reset(worldEpoch);
        lastListener=listenerPosition;hasListener=true;
        if(finite(listenerForward)) {
            const float planar=std::hypot(listenerForward.x,listenerForward.z);
            if(std::isfinite(planar)&&planar>.0001f)listenerRight={listenerForward.z/planar,0,-listenerForward.x/planar};
        }
        const bool running=advancing&&!game.paused&&validDelta;
        auto& nextEngines=engines[1-previous];auto& nextFeet=feet[1-previous];
        nextEngines.clear();nextFeet.clear();
        Selection<WorldEngineSound,WorldEngineSources> engineSelection;
        Selection<WorldFootSound,WorldFootSources> footSelection;
        const size_t vehicleCount=std::min(game.vehicles.size(),size_t(EngineCapacity));
        const size_t personCount=std::min(game.pedestrians.size(),size_t(FootCapacity));
        statistics.capacityDrops+=game.vehicles.size()-vehicleCount+game.pedestrians.size()-personCount;
        for(size_t i=0;i<vehicleCount;++i) {
            const auto& vehicle=game.vehicles[i];const auto entity=vehicle.identity;
            if(!entity||vehicle.health<=0||vehicle.parked||int(i)==game.occupied)continue;
            const int kind=int(vehicle.kind);
            if(!positionValid(vehicle.position)||!std::isfinite(vehicle.health)||!std::isfinite(vehicle.speed)||
               !std::isfinite(vehicle.throttle)||kind<0||kind>3) {++statistics.rejectedSamples;continue;}
            if(!game.world.collisionReady(vehicle.position))continue;
            if(nextEngines.find(entity)){++statistics.duplicateIdentities;continue;}
            const auto* old=engines[previous].find(entity);
            EngineHistory record;record.entity=entity;record.position=vehicle.position;record.kind=kind;
            const bool jump=old&&(old->kind!=kind||distance(record.position,old->position)>std::max(12.0f,dt*250));
            record.id=old&&!jump?old->id:admit(jump);
            nextEngines.add(record);
            const float range=distance(vehicle.position,listenerPosition);
            if(range>=EngineRadius)continue;
            WorldEngineSound sound;sound.id=record.id;sound.kind=kind;
            sound.speed=std::min(std::abs(vehicle.speed),200.0f);sound.throttle=clamp(vehicle.throttle,0,1);
            pan(vehicle.position,listenerPosition,range,EngineRadius,18,.72f,sound.left,sound.right);
            engineSelection.insert(sound,range,entity);
        }
        for(size_t i=0;i<personCount;++i) {
            const auto& person=game.pedestrians[i];const uint64_t entity=person.identity;
            if(!entity||person.health<=0)continue;
            if(!positionValid(person.position)||!std::isfinite(person.health)||!std::isfinite(person.phase)||
               std::abs(person.phase)>10000000||!std::isfinite(person.sitBlend)||!std::isfinite(person.motion)) {
                ++statistics.rejectedSamples;continue;
            }
            if(!game.world.collisionReady(person.position))continue;
            if(nextFeet.find(entity)){++statistics.duplicateIdentities;continue;}
            const auto* old=feet[previous].find(entity);
            const float moved=old?std::hypot(person.position.x-old->position.x,person.position.z-old->position.z):0;
            const float phaseDelta=old?person.phase-old->phase:0;
            const bool jump=old&&(distance(person.position,old->position)>std::max(.25f,dt*8+.08f)||
                                 phaseDelta<-.001f||phaseDelta>dt*24+.25f);
            FootHistory record=old&&!jump?*old:FootHistory{};
            if(!old||jump)record.id=admit(jump);
            record.entity=entity;record.position=person.position;record.phase=person.phase;
            const float ground=game.world.height(person.position.x,person.position.z);
            const bool wet=person.position.y<=World::WaterLevel+.05f&&game.world.waterDepth(person.position.x,person.position.z)>.05f;
            record.grounded=person.sitBlend<.05f&&std::abs(person.position.y-ground)<.08f&&!wet;
            record.canStrike=running&&record.grounded;
            record.age=std::min(record.age+dt,1.0f);
            // Gait phase is advanced by actual displacement * 3 in both civilian
            // and police simulation. Matching both rejects blocked input, pose
            // phase changes, collision pushes, and teleports. Animation motion
            // is smoothed and is deliberately not proof that a foot moved.
            const bool moving=old&&!jump&&running&&moved>.0005f&&moved<=dt*8+1e-6f;
            const bool continuous=moving&&old->canStrike&&old->grounded&&record.grounded&&
                                  phaseDelta>0&&std::abs(phaseDelta-moved*3)<.035f+moved*.35f;
            if(continuous) {
                const double before=std::floor((double(old->phase)-double(Pi)*.5)/double(Pi));
                const double after=std::floor((double(person.phase)-double(Pi)*.5)/double(Pi));
                if(after>before) {
                    const double landing=double(Pi)*.5+after*double(Pi);
                    record.age=clamp(float((double(person.phase)-landing)/double(phaseDelta))*dt,0,dt);
                    ++record.strike;if(record.strike==0)++record.strike;
                    record.side=int((static_cast<int64_t>(after)%2+2)%2);
                    record.strength=clamp((moved/dt)/4.4f,.18f,1);
                    record.surface=surface(game.world.groundSurface(person.position.x,person.position.z));
                    ++statistics.contacts;
                }
            }
            if(!running||!record.grounded){record.age=1;record.strength=0;}
            nextFeet.add(record);
            const float range=distance(person.position,listenerPosition);
            // Keep a moving source present between landings, so first admission
            // primes the DSP before its next strike. A stopped foot may finish
            // its newest short contact tail without manufacturing another one.
            const bool prime=!old&&running&&person.motion>.02f;
            if(range>=FootRadius||!record.grounded||(!moving&&!prime&&record.age>.12f))continue;
            WorldFootSound sound;sound.id=record.id;sound.strikeSerial=record.strike;
            sound.strikeAgeSeconds=record.age;sound.strength=record.strength;sound.side=record.side;sound.surface=record.surface;
            pan(person.position,listenerPosition,range,FootRadius,7,.82f,sound.left,sound.right);
            footSelection.insert(sound,range,entity);
        }
        previous=1-previous;
        statistics.trackedEngines=nextEngines.count;statistics.trackedFeet=nextFeet.count;
        result.engines=engineSelection.sounds;result.engineCount=engineSelection.count;
        result.feet=footSelection.sounds;result.footCount=footSelection.count;
        gunfire.update(result,game.lawState(),game.lawShots(),dt,running,
            [this,listenerPosition](Vec3 origin,float& leftGain,float& rightGain) {
                if(!positionValid(origin))return false;
                const float range=distance(origin,listenerPosition);
                if(range<GunRadius)pan(origin,listenerPosition,range,GunRadius,20,1,leftGain,rightGain);
                return true;
            });
        return finish(result,running);
    }
private:
    struct EngineHistory {uint64_t entity=0,id=0;Vec3 position;int kind=0;};
    struct FootHistory {
        uint64_t entity=0,id=0,strike=0;Vec3 position;float phase=0,age=1,strength=0;
        FootSurface surface=FootSurface::Pavement;int side=0;bool grounded=false,canStrike=false;
    };
    template<class T,unsigned Capacity> struct History {
        std::array<T,Capacity> records{};
        std::array<unsigned,Capacity*2> slots{};
        unsigned count=0;
        void clear() noexcept {slots.fill(0);count=0;}
        static unsigned hash(uint64_t key) noexcept {
            key^=key>>33;key*=0xff51afd7ed558ccdULL;key^=key>>33;
            return unsigned(key)&(Capacity*2-1);
        }
        const T* find(uint64_t entity) const noexcept {
            unsigned slot=hash(entity);
            for(unsigned n=0;n<Capacity*2;++n,slot=(slot+1)&(Capacity*2-1)) {
                const unsigned item=slots[slot];if(item==0)return nullptr;
                if(records[item-1].entity==entity)return &records[item-1];
            }
            return nullptr;
        }
        void add(const T& record) noexcept {
            if(count==Capacity)return;
            unsigned slot=hash(record.entity);
            while(slots[slot])slot=(slot+1)&(Capacity*2-1);
            records[count]=record;slots[slot]=++count;
        }
    };
    template<class T,unsigned Capacity> struct Selection {
        std::array<T,Capacity> sounds{};
        std::array<float,Capacity> ranges{};std::array<uint64_t,Capacity> entities{};
        unsigned count=0;
        void insert(const T& sound,float range,uint64_t entity) noexcept {
            unsigned at=0;
            while(at<count&&(ranges[at]<range||(ranges[at]==range&&entities[at]<entity)))++at;
            if(at==Capacity)return;
            if(count<Capacity)++count;
            for(unsigned i=count-1;i>at;--i){sounds[i]=sounds[i-1];ranges[i]=ranges[i-1];entities[i]=entities[i-1];}
            sounds[at]=sound;ranges[at]=range;entities[at]=entity;
        }
    };
    static bool finite(Vec3 value) noexcept {return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);}
    static bool positionValid(Vec3 value) noexcept {
        return finite(value)&&std::abs(value.x)<=World::Extent+512&&std::abs(value.z)<=World::Extent+512&&std::abs(value.y)<10000;
    }
    static float distance(Vec3 a,Vec3 b) noexcept {return length(a-b);}
    static FootSurface surface(GroundSurface value) noexcept {
        switch(value) {
        case GroundSurface::Soil:return FootSurface::Soil;
        case GroundSurface::Grass:return FootSurface::Grass;
        case GroundSurface::Sand:return FootSurface::Sand;
        case GroundSurface::Wood:return FootSurface::Wood;
        default:return FootSurface::Pavement;
        }
    }
    void pan(Vec3 source,Vec3 listener,float range,float radius,float reference,float volume,float& leftGain,float& rightGain) const noexcept {
        const float edge=clamp(1-range/radius,0,1);
        // Softened inverse-square energy with a zero-slope cutoff. At the source
        // both ears receive equal power; angular pan remains stable overhead.
        const float gain=volume*(edge*edge*(3-2*edge))/std::sqrt(1+(range/reference)*(range/reference));
        Vec3 direction=source-listener;const float horizontal=std::hypot(direction.x,direction.z);
        const float balance=horizontal>.0001f?clamp(dot(direction,listenerRight)/horizontal,-1,1):0;
        leftGain=gain*std::sqrt((1-balance)*.5f);rightGain=gain*std::sqrt((1+balance)*.5f);
    }
    uint64_t admit(bool renewal) noexcept {
        ++statistics.sourceAdmissions;if(renewal)++statistics.sourceRenewals;
        ++nextSoundId;if(nextSoundId==0)++nextSoundId;return nextSoundId;
    }
    WorldAudioState finish(WorldAudioState result,bool advancing) noexcept {
        ++statistics.snapshots;++publication;if(publication==0)++publication;
        result.epoch=audioEpoch;result.publicationSerial=publication;result.advancing=advancing;return result;
    }
    std::array<History<EngineHistory,EngineCapacity>,2> engines{};
    std::array<History<FootHistory,FootCapacity>,2> feet{};
    WorldGunfireScene gunfire;
    WorldAudioSceneStats statistics;
    uint64_t sourceWorldEpoch=0,audioEpoch=0,publication=0,nextSoundId=0;
    unsigned previous=0;bool initialized=false,hasListener=false;
    Vec3 lastListener,listenerRight{1,0,0};
};
static_assert(sizeof(WorldAudioScene)<65536,"World audio histories must remain bounded below 64 KiB");
}
