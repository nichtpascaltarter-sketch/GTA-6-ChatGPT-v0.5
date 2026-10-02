#pragma once
#include "audio.h"
#include "law.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace mc {
// Game-thread retention bridges render frames that the audio mailbox may skip.
// Only actual LawShot emissions create payloads; the full officer roster always
// publishes counters, even when no sound or no nearby officer can be heard.
class WorldGunfireScene {
public:
    static constexpr float RetentionSeconds=.45f;
    void reset() noexcept {count_=0;}
    // Spatialize validates the fixed emission origin and writes listener gains.
    // Returning false discards its payload, while still consuming its counter.
    template<class Spatialize> void update(WorldAudioState& result,const LawState& law,
        std::span<const LawShot> shots,float dt,bool advancing,Spatialize spatialize) noexcept {
        result.gunCount=0;
        const bool running=advancing&&std::isfinite(dt)&&dt>0&&dt<=.051f;
        std::array<History,WorldGunSources> next{};
        std::array<const LawUnitState*,WorldGunSources> sorted{};
        unsigned count=0;
        for(std::size_t i=0;i<std::min<std::size_t>(law.count,law.units.size());++i) {
            const auto& unit=law.units[i];
            if(unit.kind!=LawUnitKind::Officer||!unit.identity)continue;
            unsigned at=count;
            while(at&&sorted[at-1]->identity>unit.identity){sorted[at]=sorted[at-1];--at;}
            sorted[at]=&unit;++count;
        }
        for(unsigned i=0;i<count;) {
            unsigned end=i+1;while(end<count&&sorted[end]->identity==sorted[i]->identity)++end;
            const auto& unit=*sorted[i];
            if(end-i>1){i=end;continue;}i=end;
            const History* old=nullptr;
            for(unsigned k=0;k<count_;++k)if(history_[k].shooter==unit.identity){old=&history_[k];break;}
            History record=old?*old:History{};
            record.shooter=unit.identity;
            record.serial=old?std::max(old->serial,unit.weapon.shotsFired):unit.weapon.shotsFired;
            if(running) {
                record.age=std::min(record.age+dt,1.f);
                if(record.age>RetentionSeconds)record.hasEmission=false;
                if(!old||unit.weapon.shotsFired>old->serial) {
                    const LawShot* emission=nullptr;unsigned matches=0;
                    for(std::size_t s=0;s<std::min(shots.size(),LawMaxShots);++s) {
                        if(shots[s].shooter==unit.identity&&shots[s].sequence==unit.weapon.shotsFired) {
                            emission=&shots[s];++matches;
                        }
                    }
                    record.hasEmission=false;
                    if(matches==1&&emission&&finite(emission->origin)) {
                        record.origin=emission->origin;record.age=0;record.hasEmission=true;
                    }
                }
            }else {record.hasEmission=false;record.age=1;}
            WorldGunSound sound;sound.shooter=record.shooter;sound.shotSerial=record.serial;
            if(record.hasEmission) {
                sound.origin={record.origin.x,record.origin.y,record.origin.z};
                sound.ageSeconds=record.age;
                if(spatialize(record.origin,sound.left,sound.right))sound.strength=1;
                else record.hasEmission=false;
            }
            next[result.gunCount]=record;result.gunfire[result.gunCount++]=sound;
        }
        history_=next;count_=result.gunCount;
    }
private:
    struct History {
        std::uint32_t shooter=0,serial=0;
        Vec3 origin;
        float age=1;
        bool hasEmission=false;
    };
    static bool finite(Vec3 value) noexcept {
        return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
    }
    std::array<History,WorldGunSources> history_{};
    unsigned count_=0;
};
static_assert(LawMaxUnits<=WorldGunSources,"Every possible officer must retain a counter baseline");
static_assert(LawSystem::ShotInterval>WorldGunfireScene::RetentionSeconds,
              "Faster weapons need a retained event ring per shooter");
}
