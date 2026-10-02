#pragma once
#include "audio.h"
#include "world_gunfire_synth.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mc {
struct WorldAudioStats {
    unsigned engineVoices=0,footVoices=0,contacts=0;
    std::uint64_t strikes=0,droppedSources=0,droppedStrikes=0,rejectedSources=0,staleReleases=0;
    unsigned gunVoices=0,gunShooters=0;
    std::uint64_t gunshots=0,droppedGunshots=0,rejectedGunSources=0,staleGunPrimes=0;
};

// The game supplies a small, immutable snapshot. All phases, random streams,
// envelopes and release tails belong to this allocation-free audio-side mixer.
class WorldSynth {
public:
    static constexpr unsigned EngineVoices=12,FootVoices=12,ContactsPerFoot=3;
    static constexpr double FreshnessSeconds=.25,MaximumStrikeAge=.12;
    explicit WorldSynth(unsigned rate=48000) {
        // Generated once outside rendering; interpolation bounds the additional
        // world-voice work even at a 384 kHz Windows endpoint.
        for(unsigned i=0;i<=SineSteps;++i)sine_[i]=float(std::sin(Tau*double(i)/SineSteps));
        setSampleRate(rate);
    }
    void setSampleRate(unsigned rate) {
        gunfire_.setSampleRate(rate);
        rate_=double(std::clamp(rate,8000u,384000u));delta_=1.0/rate_;
        gainCoefficient_=coefficient(1.0/.020);
        controlCoefficient_=coefficient(1.0/.025);
        lowCoefficient_=coefficient(Tau*180);
        midCoefficient_=coefficient(Tau*1500);
        engineNoiseCoefficient_=coefficient(Tau*180);
        for(unsigned i=0;i<decay_.size();++i)decay_[i]=float(std::exp(-delta_/decaySeconds_[i]));
    }
    void prime(const WorldAudioState& state,bool paused=false) {gunfire_.prime(state,paused);}
    void update(const WorldAudioState& state,bool paused=false) {
        gunfire_.update(state,paused);
        const bool newEpoch=!initialized_||state.epoch!=epoch_;
        if(!newEpoch&&state.publicationSerial<serial_)return;
        const bool fresh=newEpoch||state.publicationSerial!=serial_;
        if(!fresh&&paused==paused_&&state.advancing==advancing_)return;
        if(!fresh&&stale_){paused_=paused;advancing_=state.advancing;return;}
        const bool prime=newEpoch||paused_||!advancing_||stale_;
        if(newEpoch) {
            for(auto& voice:engines_)voice.selected=false;
            for(auto& voice:feet_)voice.selected=false;
        }
        initialized_=true;epoch_=state.epoch;serial_=state.publicationSerial;
        paused_=paused;advancing_=state.advancing;
        if(fresh){age_=0;stale_=false;}
        for(auto& voice:engines_){voice.wasSelected=voice.selected;voice.selected=false;voice.targetLeft=voice.targetRight=0;}
        for(auto& voice:feet_){voice.wasSelected=voice.selected;voice.selected=false;voice.targetLeft=voice.targetRight=0;}

        canonical(state.engines,state.engineCount,[&](const WorldEngineSound& sound) {
            if(sound.kind<0||sound.kind>3||!validGain(sound.left)||!validGain(sound.right)||
               !std::isfinite(sound.speed)||!std::isfinite(sound.throttle)) {++stats_.rejectedSources;return;}
            Engine* voice=find(engines_,sound.id);
            // A class change requires a new entity generation. Retire the old
            // oscillator rather than replacing its waveform while audible.
            if(voice&&voice->kind!=sound.kind){++stats_.rejectedSources;return;}
            if(!voice) {
                voice=vacant(engines_);
                if(!voice){++stats_.droppedSources;return;}
                *voice={};voice->used=true;voice->id=sound.id;voice->epoch=epoch_;
                voice->random=seed(sound.id,epoch_,0x762d4f31u);
                voice->phase=double(voice->random&0xffffu)/65536.0;
                voice->kind=sound.kind;voice->speed=std::min(150.f,std::abs(sound.speed));anyVoice_=true;
            }
            voice->selected=true;
            voice->targetLeft=paused?0:std::min(1.f,sound.left);
            voice->targetRight=paused?0:std::min(1.f,sound.right);
            voice->targetSpeed=std::min(150.f,std::abs(sound.speed));
            voice->targetThrottle=std::clamp(sound.throttle,0.f,1.f);
        });
        canonical(state.feet,state.footCount,[&](const WorldFootSound& sound) {
            const int surface=int(sound.surface);
            if(!validGain(sound.left)||!validGain(sound.right)||!std::isfinite(sound.strength)||
               !std::isfinite(sound.strikeAgeSeconds)||sound.strikeAgeSeconds<0||surface<0||surface>4||
               sound.side<0||sound.side>1){++stats_.rejectedSources;return;}
            Foot* voice=find(feet_,sound.id);
            bool added=false;
            if(!voice) {
                voice=vacant(feet_);
                if(!voice){++stats_.droppedSources;return;}
                *voice={};voice->used=true;voice->id=sound.id;voice->epoch=epoch_;
                voice->strikeSerial=sound.strikeSerial;added=true;anyVoice_=true;
            }
            voice->selected=true;
            voice->targetLeft=paused?0:std::min(1.f,sound.left);
            voice->targetRight=paused?0:std::min(1.f,sound.right);
            if(sound.strikeSerial>voice->strikeSerial) {
                if(!added&&voice->wasSelected&&!prime&&!paused&&state.advancing&&fresh&&sound.strikeAgeSeconds<=MaximumStrikeAge)
                    strike(*voice,sound);
                voice->strikeSerial=sound.strikeSerial;
            }
        });
    }
    void mix(float& left,float& right) {
        if(!initialized_||!anyVoice_) {
            float worldLeft=0,worldRight=0;
            gunfire_.mix(worldLeft,worldRight,[this](double phase){return wave(phase);});
            addBus(left,right,worldLeft,worldRight);return;
        }
        age_+=delta_;
        if(!stale_&&age_>FreshnessSeconds) {
            stale_=true;++stats_.staleReleases;
            for(auto& voice:engines_){voice.selected=false;voice.targetLeft=voice.targetRight=0;}
            for(auto& voice:feet_){voice.selected=false;voice.targetLeft=voice.targetRight=0;}
        }
        float worldLeft=0,worldRight=0;anyVoice_=false;
        for(auto& voice:engines_) {
            if(!voice.used)continue;
            smoothGains(voice);
            if(!voice.selected&&silent(voice)){voice.used=false;continue;}
            anyVoice_=true;
            voice.speed+=(voice.targetSpeed-voice.speed)*controlCoefficient_;
            voice.throttle+=(voice.targetThrottle-voice.throttle)*controlCoefficient_;
            double frequency=0;
            switch(voice.kind) {
            case 0:frequency=25+std::fmod(voice.speed,13.f)*5.3+voice.speed*1.9;break;
            case 1:frequency=42+voice.speed*4.2;break;
            case 2:frequency=18+voice.speed*2.3;break;
            default:frequency=35+voice.throttle*45+voice.speed*.25;break;
            }
            advance(voice.phase,frequency);
            // A generated sine table supplies all three authored harmonics.
            const float fundamental=wave(voice.phase);
            const float second=2*fundamental*wave(voice.phase+.25);
            const float third=3*fundamental-4*fundamental*fundamental*fundamental;
            voice.low+=(noise(voice.random)-voice.low)*engineNoiseCoefficient_;
            static constexpr float a[4]={.048f,.037f,.056f,.061f};
            static constexpr float b[4]={.023f,.031f,.015f,.027f};
            static constexpr float c[4]={.012f,.024f,.006f,.018f};
            const float sample=(a[voice.kind]*fundamental+b[voice.kind]*second+c[voice.kind]*third+
                .012f*voice.low)*(.80f+.20f*voice.throttle);
            worldLeft+=sample*voice.left;worldRight+=sample*voice.right;
        }
        for(auto& voice:feet_) {
            if(!voice.used)continue;
            smoothGains(voice);
            bool sounding=false;float sample=0;
            for(auto& contact:voice.contacts) {
                if(contact.age>=ContactDuration)continue;
                sounding=true;
                const float white=noise(contact.random);
                contact.low+=(white-contact.low)*lowCoefficient_;
                contact.mid+=(white-contact.mid)*midCoefficient_;
                const float edge=float(std::min(1.0,contact.age/.003)*std::min(1.0,(ContactDuration-contact.age)/.025));
                const float body=wave(contact.phase);
                float texture=0,tone=0;
                switch(contact.surface) {
                case 0:tone=body*.75f;texture=white*.70f+contact.mid*.25f;break;
                case 1:tone=body*.42f;texture=contact.mid*.95f+white*.15f;break;
                case 2:tone=body*.26f;texture=(contact.mid-contact.low)*.92f;break;
                case 3:tone=body*.34f;texture=contact.low*.90f+contact.mid*.10f;break;
                default:tone=(body+.4f*wave(contact.age*contact.frequency*1.83))*.85f;texture=contact.mid*.25f;break;
                }
                sample+=(tone+texture)*edge*contact.envelope*contact.strength;
                contact.envelope*=decay_[contact.surface];
                advance(contact.phase,contact.frequency);contact.age+=delta_;
            }
            if(!voice.selected&&(!sounding||silent(voice))){voice.used=false;continue;}
            anyVoice_=true;
            worldLeft+=sample*voice.left;worldRight+=sample*voice.right;
        }
        // This bus is bounded independently of the existing full-mix limiter.
        // Adding exact zero leaves the established empty-world waveform intact.
        gunfire_.mix(worldLeft,worldRight,[this](double phase){return wave(phase);});
        addBus(left,right,worldLeft,worldRight);
    }
    void render(float* stereo,std::size_t frames) {
        for(std::size_t i=0;i<frames;++i){float left=0,right=0;mix(left,right);stereo[i*2]=left;stereo[i*2+1]=right;}
    }
    WorldAudioStats stats() const {
        WorldAudioStats result=stats_;
        const auto guns=gunfire_.stats();
        result.gunVoices=guns.voices;result.gunShooters=guns.shooters;result.gunshots=guns.shots;
        result.droppedGunshots=guns.droppedShots;result.rejectedGunSources=guns.rejectedSources;result.staleGunPrimes=guns.stalePrimes;
        for(const auto& voice:engines_)result.engineVoices+=voice.used?1u:0u;
        for(const auto& voice:feet_)if(voice.used){++result.footVoices;for(const auto& contact:voice.contacts)result.contacts+=contact.age<ContactDuration?1u:0u;}
        return result;
    }
private:
    static void addBus(float& left,float& right,float worldLeft,float worldRight) {
        if(worldLeft!=0)left+=worldLeft/(1+std::abs(worldLeft));
        if(worldRight!=0)right+=worldRight/(1+std::abs(worldRight));
    }
    static constexpr double Tau=6.2831853071795864769,ContactDuration=.32;
    static constexpr unsigned SineSteps=2048;
    static constexpr std::array<double,5> decaySeconds_{{.029,.064,.064,.070,.050}};
    struct SpatialVoice {
        std::uint64_t id=0,epoch=0;
        float left=0,right=0,targetLeft=0,targetRight=0;
        bool used=false,selected=false,wasSelected=false;
    };
    struct Engine:SpatialVoice {
        double phase=0;
        float speed=0,targetSpeed=0,throttle=0,targetThrottle=0,low=0;
        std::uint32_t random=1;
        int kind=0;
    };
    struct Contact {
        double age=ContactDuration,phase=0,frequency=85;
        float envelope=0,strength=0,low=0,mid=0;
        std::uint32_t random=1;
        int surface=0;
    };
    struct Foot:SpatialVoice {
        std::uint64_t strikeSerial=0;
        std::array<Contact,ContactsPerFoot> contacts{};
    };
    std::array<Engine,EngineVoices> engines_{};
    WorldGunfireSynth gunfire_{};
    std::array<Foot,FootVoices> feet_{};
    std::array<float,5> decay_{};
    std::array<float,SineSteps+1> sine_{};
    WorldAudioStats stats_{};
    std::uint64_t epoch_=0,serial_=0;
    double rate_=48000,delta_=1.0/48000,age_=0;
    float gainCoefficient_=0,controlCoefficient_=0,lowCoefficient_=0,midCoefficient_=0,engineNoiseCoefficient_=0;
    bool initialized_=false,paused_=false,advancing_=true,stale_=false,anyVoice_=false;
    float coefficient(double rate) const {return float(1-std::exp(-rate*delta_));}
    static bool validGain(float value){return std::isfinite(value)&&value>=0;}
    static std::uint32_t hash(std::uint32_t value){value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);}
    static std::uint32_t seed(std::uint64_t id,std::uint64_t epoch,std::uint32_t salt) {
        const auto value=hash(std::uint32_t(id)^hash(std::uint32_t(id>>32))^hash(std::uint32_t(epoch))^hash(std::uint32_t(epoch>>32))^salt);
        return value?value:1u;
    }
    static float noise(std::uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return float(state>>8)*(2.f/16777215.f)-1;}
    float wave(double phase) const {
        if(phase>=1)phase-=std::floor(phase);
        const double index=phase*SineSteps;
        const auto integer=unsigned(index);
        return sine_[integer]+(sine_[integer+1]-sine_[integer])*float(index-integer);
    }
    void advance(double& phase,double frequency) const {phase+=std::min(frequency,rate_*.18)*delta_;if(phase>=1)phase-=1;}
    void smoothGains(SpatialVoice& voice) const {
        voice.left+=(voice.targetLeft-voice.left)*gainCoefficient_;
        voice.right+=(voice.targetRight-voice.right)*gainCoefficient_;
        if(voice.targetLeft==0&&voice.left<.0000001f)voice.left=0;
        if(voice.targetRight==0&&voice.right<.0000001f)voice.right=0;
    }
    static bool silent(const SpatialVoice& voice){return voice.left<.00001f&&voice.right<.00001f;}
    template<class Voice,std::size_t N> Voice* find(std::array<Voice,N>& voices,std::uint64_t id) {
        for(auto& voice:voices)if(voice.used&&voice.id==id&&voice.epoch==epoch_)return &voice;
        return nullptr;
    }
    template<class Voice,std::size_t N> static Voice* vacant(std::array<Voice,N>& voices) {
        for(auto& voice:voices)if(!voice.used)return &voice;
        return nullptr;
    }
    template<class Source,std::size_t N,class Callback> void canonical(const std::array<Source,N>& sources,unsigned count,Callback callback) {
        if(count>N){++stats_.rejectedSources;count=unsigned(N);}
        std::array<const Source*,N> sorted{};
        for(unsigned i=0;i<count;++i){unsigned at=i;while(at&&sorted[at-1]->id>sources[i].id){sorted[at]=sorted[at-1];--at;}sorted[at]=&sources[i];}
        for(unsigned i=0;i<count;) {
            unsigned end=i+1;while(end<count&&sorted[end]->id==sorted[i]->id)++end;
            if(sorted[i]->id==0||end-i>1)stats_.rejectedSources+=end-i;
            else callback(*sorted[i]);
            i=end;
        }
    }
    void strike(Foot& voice,const WorldFootSound& sound) {
        Contact* contact=nullptr;
        for(auto& candidate:voice.contacts)if(candidate.age>=ContactDuration){contact=&candidate;break;}
        if(!contact){++stats_.droppedStrikes;return;}
        *contact={};contact->surface=int(sound.surface);contact->age=sound.strikeAgeSeconds;
        contact->random=seed(voice.id,sound.strikeSerial,0xb19f04adu^std::uint32_t(sound.side));
        const float variation=.94f+.06f*noise(contact->random);
        contact->strength=std::clamp(sound.strength,0.f,1.f)*.13f*variation;
        static constexpr double frequency[5]={85,68,58,61,142};
        contact->frequency=frequency[contact->surface]*(1+.065*noise(contact->random));
        contact->phase=std::fmod(contact->age*contact->frequency,1.0);
        contact->envelope=float(std::exp(-contact->age/decaySeconds_[contact->surface]));
        ++stats_.strikes;
    }
};
}
