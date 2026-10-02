#pragma once
#include "audio.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mc {
struct WorldGunfireStats {
    std::uint64_t shots=0,droppedShots=0,rejectedSources=0,stalePrimes=0;
    unsigned voices=0,shooters=0;
};

// A counter ledger is separate from the audible voice bank. Missing, distant,
// malformed and capacity-dropped emissions are consumed, never deferred shots.
class WorldGunfireSynth {
public:
    static constexpr unsigned VoiceCapacity=8;
    static constexpr double Duration=.45,MaximumAge=.12,Freshness=.25;
    explicit WorldGunfireSynth(unsigned rate=48000) {setSampleRate(rate);}
    void setSampleRate(unsigned rate) noexcept {
        rate_=double(std::clamp(rate,8000u,384000u));delta_=1/rate_;
        gainCoefficient_=float(1-std::exp(-delta_/.010));
        lowCoefficient_=float(1-std::exp(-delta_*Tau*180));
        midCoefficient_=float(1-std::exp(-delta_*Tau*std::min(1600.0,rate_*.42)));
        crackDecay_=float(std::exp(-delta_/.014));bodyDecay_=float(std::exp(-delta_/.080));
        tailDecay_=float(std::exp(-delta_/.120));
    }
    void prime(const WorldAudioState& state,bool paused=false) {update(state,paused,true);}
    void update(const WorldAudioState& state,bool paused=false,bool forcePrime=false) {
        const bool newEpoch=!initialized_||state.epoch!=epoch_;
        if(!newEpoch&&state.publicationSerial<serial_)return;
        const bool fresh=newEpoch||state.publicationSerial!=serial_;
        // A pause flag can arrive without a new scene publication. It may mute
        // tails but cannot refresh the clock or submit the same event again.
        if(paused)release();
        if(!fresh&&!forcePrime)return;
        const bool baseline=forcePrime||newEpoch||stale_;
        if(stale_&&fresh)++statistics_.stalePrimes;
        if(newEpoch||forcePrime)release();
        if(newEpoch)ledgerCount_=0;
        initialized_=true;epoch_=state.epoch;serial_=state.publicationSerial;
        if(fresh){age_=0;stale_=false;}

        unsigned count=std::min(state.gunCount,WorldGunSources);
        if(state.gunCount>WorldGunSources)++statistics_.rejectedSources;
        std::array<const WorldGunSound*,WorldGunSources> sorted{},starts{};
        std::array<Ledger,WorldGunSources> next{};
        unsigned nextCount=0,startCount=0;
        for(unsigned i=0;i<count;++i) {
            unsigned at=i;
            while(at&&sorted[at-1]->shooter>state.gunfire[i].shooter){sorted[at]=sorted[at-1];--at;}
            sorted[at]=&state.gunfire[i];
        }
        for(unsigned i=0;i<count;) {
            unsigned end=i+1;while(end<count&&sorted[end]->shooter==sorted[i]->shooter)++end;
            const auto& sound=*sorted[i];
            if(sound.shooter==0||end-i>1){statistics_.rejectedSources+=end-i;i=end;continue;}
            i=end;
            const Ledger* old=nullptr;
            for(unsigned k=0;k<ledgerCount_;++k)if(ledger_[k].shooter==sound.shooter){old=&ledger_[k];break;}
            // Even malformed payloads advance a valid identity's high-water mark.
            next[nextCount++]={sound.shooter,old?std::max(old->serial,sound.shotSerial):sound.shotSerial};
            if(!valid(sound)){++statistics_.rejectedSources;continue;}
            if(sound.strength>0)for(auto& voice:voices_) {
                if(voice.used&&voice.epoch==epoch_&&voice.shooter==sound.shooter&&voice.serial==sound.shotSerial) {
                    voice.targetLeft=std::min(sound.left,1.f);voice.targetRight=std::min(sound.right,1.f);
                }
            }
            if(!old||sound.shotSerial<=old->serial||baseline||paused||!state.advancing||!fresh||
               sound.ageSeconds>MaximumAge||sound.strength<=0||(sound.left==0&&sound.right==0))continue;
            // Loudest new shots get free slots first. Stable tie breaking and a
            // canonical ledger make source array permutations sample-identical.
            unsigned at=startCount;
            while(at&&before(sound,*starts[at-1])){starts[at]=starts[at-1];--at;}
            starts[at]=&sound;++startCount;
        }
        ledger_=next;ledgerCount_=nextCount;
        for(unsigned i=0;i<startCount;++i)start(*starts[i]);
    }
    // The caller supplies its generated sine table. No new lookup table or
    // player/radio random state is needed for this independent transient path.
    template<class Wave> void mix(float& left,float& right,Wave wave) noexcept {
        if(!initialized_)return;
        // Baseline-only rosters must age too: no audible voice is required for
        // a later device stall or missed publication to require re-priming.
        if(age_<=Freshness){age_+=delta_;if(age_>Freshness)stale_=true;}
        if(!anyVoice_)return;
        anyVoice_=false;
        for(auto& voice:voices_) {
            if(!voice.used)continue;
            if(voice.age>=Duration||(voice.releasing&&voice.releaseRemaining<=0)){voice.used=false;continue;}
            anyVoice_=true;
            voice.left+=(voice.targetLeft-voice.left)*gainCoefficient_;
            voice.right+=(voice.targetRight-voice.right)*gainCoefficient_;
            const float white=noise(voice.random);
            voice.low+=(white-voice.low)*lowCoefficient_;
            voice.mid+=(white-voice.mid)*midCoefficient_;
            const float attack=float(std::min(1.0,voice.admissionAge/.001));
            const float edge=float(std::min(1.0,(Duration-voice.age)/.020));
            const float releaseGain=voice.releasing?float(std::max(0.0,voice.releaseRemaining)/.020):1.f;
            const float sample=(.34f*(white-voice.mid)*voice.crack+
                .16f*(wave(voice.phase)+.65f*voice.low)*voice.body+
                .065f*voice.mid*voice.tail)*voice.strength*attack*edge*releaseGain;
            left+=sample*voice.left;right+=sample*voice.right;
            voice.crack*=crackDecay_;voice.body*=bodyDecay_;voice.tail*=tailDecay_;
            voice.phase+=voice.frequency*delta_;if(voice.phase>=1)voice.phase-=1;
            voice.age+=delta_;voice.admissionAge+=delta_;
            if(voice.releasing)voice.releaseRemaining-=delta_;
        }
    }
    WorldGunfireStats stats() const noexcept {
        auto result=statistics_;result.shooters=ledgerCount_;
        for(const auto& voice:voices_)result.voices+=voice.used?1u:0u;
        return result;
    }
private:
    static constexpr double Tau=6.2831853071795864769;
    struct Ledger {std::uint32_t shooter=0,serial=0;};
    struct Voice {
        std::uint64_t epoch=0;
        std::uint32_t shooter=0,serial=0,random=1;
        double age=Duration,admissionAge=0,phase=0,frequency=110,releaseRemaining=.020;
        float left=0,right=0,targetLeft=0,targetRight=0,strength=0;
        float low=0,mid=0,crack=0,body=0,tail=0;
        bool used=false,releasing=false;
    };
    static bool valid(const WorldGunSound& sound) noexcept {
        for(float value:sound.origin)if(!std::isfinite(value))return false;
        return std::isfinite(sound.left)&&sound.left>=0&&std::isfinite(sound.right)&&sound.right>=0&&
            std::isfinite(sound.ageSeconds)&&sound.ageSeconds>=0&&std::isfinite(sound.strength)&&sound.strength>=0;
    }
    static bool before(const WorldGunSound& a,const WorldGunSound& b) noexcept {
        const double aLeft=std::min(a.left,1.f),aRight=std::min(a.right,1.f);
        const double bLeft=std::min(b.left,1.f),bRight=std::min(b.right,1.f);
        const double aStrength=std::min(a.strength,1.f),bStrength=std::min(b.strength,1.f);
        const double powerA=(aLeft*aLeft+aRight*aRight)*aStrength*aStrength;
        const double powerB=(bLeft*bLeft+bRight*bRight)*bStrength*bStrength;
        return powerA!=powerB?powerA>powerB:(a.shooter!=b.shooter?a.shooter<b.shooter:a.shotSerial<b.shotSerial);
    }
    static std::uint32_t hash(std::uint32_t value) noexcept {
        value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);
    }
    static float noise(std::uint32_t& state) noexcept {
        state^=state<<13;state^=state>>17;state^=state<<5;return float(state>>8)*(2.f/16777215.f)-1;
    }
    void release() noexcept {
        for(auto& voice:voices_)if(voice.used)voice.releasing=true;
    }
    void start(const WorldGunSound& sound) noexcept {
        Voice* slot=nullptr;for(auto& voice:voices_)if(!voice.used){slot=&voice;break;}
        if(!slot){++statistics_.droppedShots;return;}
        auto& voice=*slot;voice={};voice.used=true;voice.epoch=epoch_;
        voice.shooter=sound.shooter;voice.serial=sound.shotSerial;
        voice.random=hash(sound.shooter^hash(sound.shotSerial)^hash(std::uint32_t(epoch_))^
                          hash(std::uint32_t(epoch_>>32))^0xf18c734du);
        if(!voice.random)voice.random=1;
        voice.strength=std::min(sound.strength,1.f)*(.96f+.04f*noise(voice.random));
        voice.frequency=110+15*noise(voice.random);voice.age=double(sound.ageSeconds);
        voice.phase=std::fmod(voice.age*voice.frequency,1.0);
        voice.crack=float(std::exp(-voice.age/.014));voice.body=float(std::exp(-voice.age/.080));
        voice.tail=float(std::exp(-voice.age/.120));
        voice.left=voice.targetLeft=std::min(sound.left,1.f);voice.right=voice.targetRight=std::min(sound.right,1.f);
        anyVoice_=true;++statistics_.shots;
    }
    std::array<Ledger,WorldGunSources> ledger_{};
    std::array<Voice,VoiceCapacity> voices_{};
    WorldGunfireStats statistics_{};
    unsigned ledgerCount_=0;
    std::uint64_t epoch_=0,serial_=0;
    double rate_=48000,delta_=1.0/48000,age_=0;
    float gainCoefficient_=0,lowCoefficient_=0,midCoefficient_=0,crackDecay_=0,bodyDecay_=0,tailDecay_=0;
    bool initialized_=false,stale_=false,anyVoice_=false;
};
}
