#pragma once
#include "audio.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mc {

// All instruments, rhythms and arrangements are generated here from first principles.
// This class has no operating-system dependencies and never allocates while rendering.
class Synth {
public:
    explicit Synth(unsigned sampleRate=48000) { setSampleRate(sampleRate); }
    void setSampleRate(unsigned rate) {
        sampleRate_=static_cast<double>(std::max(8000u,std::min(rate,384000u)));
        delta_=1.0/sampleRate_;
        smooth_=static_cast<float>(1.0-std::exp(-delta_/0.025));
        lowpass_=static_cast<float>(1.0-std::exp(-delta_*850.0));
    }
    void update(const AudioState& value) {
        const float newShot=finiteClamp(value.shot,0,1);
        if(newShot>previousShot_+0.005f) {
            // A new muzzle flash restarts the brief blast; the tail is synthesized noise.
            shotAge_=0;
            shotStrength_=0.55f+0.45f*newShot;
        }
        previousShot_=newShot;
        state_=value;
        state_.speed=finiteClamp(value.speed,-150,150);
        state_.engine=finiteClamp(value.engine,0,1);
        state_.rain=finiteClamp(value.rain,0,1);
        state_.wanted=finiteClamp(value.wanted,0,5);
        state_.volume=finiteClamp(value.volume,0,1);
        state_.station=std::max(0,std::min(3,value.station));
        state_.engineKind=std::max(0,std::min(3,value.engineKind));
        state_.throttle=finiteClamp(value.throttle,0,1);
        state_.shore=finiteClamp(value.shore,0,1);
        state_.nature=finiteClamp(value.nature,0,1);
        state_.urban=finiteClamp(value.urban,0,1);
        if(state_.station!=station_) {
            station_=state_.station;
            step_=0;
            samplesToStep_=0;
            for(auto& voice:voices_) voice.release=0.016f;
        }
    }
    void render(float* stereo,std::size_t frames) {
        for(std::size_t i=0;i<frames;++i) {
            const float desiredGain=state_.paused?0:state_.volume;
            master_+=(desiredGain-master_)*smooth_;
            if(desiredGain==0 && master_<0.000001f) master_=0;
            if(state_.paused && master_==0) {
                stereo[i*2]=stereo[i*2+1]=0;
                continue;
            }
            if(station_>0 && samplesToStep_--<=0) sequence();
            float left=0,right=0;
            for(auto& voice:voices_) {
                if(!voice.active) continue;
                const float x=renderVoice(voice);
                left+=x*voice.left;
                right+=x*voice.right;
            }
            // Short asymmetric room echoes give the generated instruments stereo space.
            const float echoL=delayL_[delayIndex_];
            const float echoR=delayR_[delayIndex_];
            delayL_[delayIndex_]=left*0.23f+echoR*0.25f;
            delayR_[delayIndex_]=right*0.21f+echoL*0.29f;
            delayIndex_=(delayIndex_+1)%delayL_.size();
            left+=echoL;right+=echoR;

            engine_+=(state_.engine-engine_)*smooth_;
            speed_+=(std::abs(state_.speed)-speed_)*smooth_;
            rain_+=(state_.rain-rain_)*smooth_;
            wanted_+=(state_.wanted-wanted_)*smooth_;
            throttle_+=(state_.throttle-throttle_)*smooth_;
            shore_+=(state_.shore-shore_)*smooth_;
            nature_+=(state_.nature-nature_)*smooth_;
            urban_+=(state_.urban-urban_)*smooth_;
            for(int kind=0;kind<4;++kind)engineMix_[kind]+=((state_.engineKind==kind?1.f:0.f)-engineMix_[kind])*smooth_;
            const float whiteL=noise(),whiteR=noise();
            lowNoiseL_+=(whiteL-lowNoiseL_)*lowpass_;
            lowNoiseR_+=(whiteR-lowNoiseR_)*lowpass_;
            const double carRevs=25.0+std::fmod(speed_,13.0f)*5.3+speed_*1.9;
            const double revs=engineMix_[0]*carRevs+engineMix_[1]*(42+speed_*4.2)+engineMix_[2]*(18+speed_*2.3)+engineMix_[3]*(35+throttle_*45+speed_*.25);
            advance(enginePhase_,revs);
            const float fundamental=.065f*engineMix_[0]+.046f*engineMix_[1]+.077f*engineMix_[2]+.088f*engineMix_[3];
            const float second=.031f*engineMix_[0]+.04f*engineMix_[1]+.018f*engineMix_[2]+.035f*engineMix_[3];
            const float third=.017f*engineMix_[0]+.031f*engineMix_[1]+.007f*engineMix_[2]+.024f*engineMix_[3];
            const float motor=engine_*(fundamental*wave(enginePhase_)+second*wave(enginePhase_*2.0)+third*wave(enginePhase_*3.0)+.014f*lowNoiseL_);
            const float wind=std::min(speed_/70.0f,1.0f)*0.05f;
            left+=motor+wind*lowNoiseL_+rain_*(0.044f*whiteL+0.035f*lowNoiseL_);
            right+=motor+wind*lowNoiseR_+rain_*(0.044f*whiteR+0.035f*lowNoiseR_);
            // Slow noise envelopes suggest breaking surf and distant traffic;
            // continuously running phases keep biome transitions click-free.
            advance(surfPhase_,.135);advance(birdCycle_,.19);advance(birdPhase_,2150+650*wave(birdCycle_*7));
            advance(urbanPhase_,.037);advance(hornPhase_,180);
            const float surf=.019f+.029f*std::pow(.5f+.5f*wave(surfPhase_),2.f);
            const float birdEnvelope=std::pow(std::max(0.f,wave(birdCycle_)),18.f)*std::pow(.5f+.5f*wave(birdCycle_*11),2.f);
            const float birds=.018f*birdEnvelope*wave(birdPhase_);
            const float hornEnvelope=std::pow(std::max(0.f,wave(urbanPhase_)),64.f);
            const float distantHorn=.008f*hornEnvelope*(wave(hornPhase_)+.3f*wave(hornPhase_*1.5));
            const float wake=engine_*engineMix_[2]*std::min(speed_/20.f,1.f)*.038f;
            left+=(shore_*surf+wake)*lowNoiseL_+nature_*(birds*.7f+.006f*lowNoiseL_)+urban_*(.012f*lowNoiseL_+distantHorn);
            right+=(shore_*surf+wake)*lowNoiseR_+nature_*(birds+.006f*lowNoiseR_)+urban_*(.012f*lowNoiseR_+distantHorn*.65f);
            advance(sirenSweep_,0.43);
            advance(sirenPhase_,620.0+350.0*(0.5+0.5*wave(sirenSweep_)));
            const float siren=std::min(wanted_,1.0f)*0.044f*(wave(sirenPhase_)+0.22f*wave(sirenPhase_*3));
            left+=siren*0.8f;right+=siren;
            if(shotAge_<0.7) {
                const float blast=shotStrength_*(whiteL*0.31f*static_cast<float>(std::exp(-shotAge_*47.0))+
                    lowNoiseL_*0.48f*static_cast<float>(std::exp(-shotAge_*12.0)));
                left+=blast;right+=blast*0.92f;
                shotAge_+=delta_;
            }
            // A continuous, symmetric soft limiter prevents inter-source clipping.
            stereo[i*2]=limit(left)*master_;
            stereo[i*2+1]=limit(right)*master_;
        }
    }
private:
    enum class Instrument { Keys, Bass, Pad, Pluck, Kick, Snare, Hat };
    struct Voice {
        bool active=false;
        Instrument type=Instrument::Keys;
        double phase=0,age=0,frequency=0,duration=0;
        float amplitude=0,left=0.7071f,right=0.7071f,release=0;
    };
    static constexpr double tau_=6.2831853071795864769;
    AudioState state_{};
    std::array<Voice,48> voices_{};
    std::array<float,8192> delayL_{},delayR_{};
    std::size_t delayIndex_=0;
    double sampleRate_=48000,delta_=1.0/48000.0,samplesToStep_=0;
    double enginePhase_=0,sirenPhase_=0,sirenSweep_=0,shotAge_=1;
    double surfPhase_=0,birdCycle_=0,birdPhase_=0,urbanPhase_=0,hornPhase_=0;
    float smooth_=0,lowpass_=0,master_=0,engine_=0,speed_=0,rain_=0,wanted_=0;
    float throttle_=0,shore_=0,nature_=0,urban_=0;
    std::array<float,4> engineMix_{{1,0,0,0}};
    float lowNoiseL_=0,lowNoiseR_=0,previousShot_=0,shotStrength_=0;
    std::uint32_t random_=0xb7e15162u,step_=0;
    int station_=0;
    static float finiteClamp(float x,float lo,float hi) {return std::isfinite(x)?std::max(lo,std::min(hi,x)):lo;}
    static float wave(double phase) {return static_cast<float>(std::sin(tau_*phase));}
    static float limit(float x) {return x/(1.0f+std::abs(x));}
    void advance(double& phase,double hz) {phase+=hz*delta_;phase-=std::floor(phase);}
    float noise() {
        random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
        return static_cast<float>(random_>>8)*(2.0f/16777215.0f)-1.0f;
    }
    void note(Instrument type,int midi,float amplitude,double duration,float pan=0) {
        Voice* selected=&voices_[0];
        double oldest=-1;
        for(auto& voice:voices_) {
            if(!voice.active) {selected=&voice;break;}
            if(voice.age>oldest) {oldest=voice.age;selected=&voice;}
        }
        *selected={};selected->active=true;selected->type=type;
        selected->frequency=440.0*std::exp2((midi-69)/12.0);
        selected->duration=duration;selected->amplitude=amplitude;
        selected->left=std::sqrt((1.0f-pan)*0.5f);
        selected->right=std::sqrt((1.0f+pan)*0.5f);
    }
    float renderVoice(Voice& voice) {
        const double age=voice.age;
        if(age>=voice.duration || voice.amplitude<0.000001f) {voice.active=false;return 0;}
        float result=0;
        double frequency=voice.frequency;
        const float edge=static_cast<float>(std::min(1.0,age/0.004)*std::min(1.0,(voice.duration-age)/0.025));
        switch(voice.type) {
        case Instrument::Keys:
            result=(wave(voice.phase)+0.30f*wave(voice.phase*2.0)+0.13f*wave(voice.phase*3.0))
                *static_cast<float>(std::exp(-age*3.0))*edge;break;
        case Instrument::Bass:
            result=(wave(voice.phase)+0.24f*wave(voice.phase*2.0))
                *static_cast<float>(std::exp(-age*2.4))*edge;break;
        case Instrument::Pad:
            result=(wave(voice.phase)+0.30f*wave(voice.phase*2.0)+0.15f*wave(voice.phase*3.0))
                *static_cast<float>(std::min(1.0,age/0.24)*std::min(1.0,(voice.duration-age)/0.4));break;
        case Instrument::Pluck:
            result=(wave(voice.phase)+0.27f*wave(voice.phase*2.0)+0.12f*wave(voice.phase*4.0))
                *static_cast<float>(std::exp(-age*7.0))*edge;break;
        case Instrument::Kick:
            frequency=42+110*std::exp(-age*30);
            result=wave(voice.phase)*static_cast<float>(std::exp(-age*14.0))+
                noise()*0.07f*static_cast<float>(std::exp(-age*180.0));break;
        case Instrument::Snare:
            result=(noise()*0.7f+wave(voice.phase)*0.3f)*static_cast<float>(std::exp(-age*24.0))*edge;break;
        case Instrument::Hat:
            result=noise()*static_cast<float>(std::exp(-age*60.0))*edge;break;
        }
        advance(voice.phase,frequency);voice.age+=delta_;
        const float output=result*voice.amplitude;
        if(voice.release>0) voice.amplitude*=static_cast<float>(std::exp(-delta_/voice.release));
        return output;
    }
    void sequence() {
        const unsigned tick=step_%16,bar=(step_/16)%16;
        const double bpm=station_==1?104:(station_==2?76:122);
        const double beat=60.0/bpm;
        // Two bars per chord and a sixteen-bar arrangement keep the radio evolving.
        static constexpr int coastal[4][4]={{50,57,60,64},{46,53,57,60},{53,60,64,67},{48,55,58,62}};
        static constexpr int night[4][4]={{48,55,58,62},{44,51,55,58},{53,60,63,67},{43,50,53,58}};
        static constexpr int drive[4][4]={{45,52,57,60},{41,48,53,57},{48,55,60,64},{43,50,55,59}};
        const int* chord=station_==1?coastal[(bar/2)%4]:(station_==2?night[(bar/2)%4]:drive[(bar/2)%4]);
        if(station_==1) {
            if(tick%4==0) note(Instrument::Kick,36,0.14f,0.36);
            if(tick==4 || tick==12) note(Instrument::Snare,50,0.062f,0.23,0.08f);
            if(tick%2==0) note(Instrument::Hat,90,0.032f,tick%4==2?0.16:0.08,0.43f);
            if(tick==0 || tick==6 || tick==10) note(Instrument::Bass,chord[0]-12,0.12f,beat*0.75);
            if(tick==2 || tick==8 || tick==14) for(int n=0;n<4;++n)
                note(Instrument::Keys,chord[n]+12,0.044f,beat*1.7,(n-1.5f)*0.32f);
            if(bar%4>=2 && tick%4==3) note(Instrument::Pluck,chord[(tick/4+bar)%4]+24,0.039f,beat, -0.25f);
        } else if(station_==2) {
            if(tick==0 || tick==7 || (tick==10 && bar%2)) note(Instrument::Kick,36,0.12f,0.40);
            if(tick==4 || tick==12) note(Instrument::Snare,48,0.052f,0.23,-0.10f);
            if(tick%2==0) note(Instrument::Hat,90,0.020f,0.08,0.5f);
            if(tick==0 || tick==10) note(Instrument::Bass,chord[0]-12,0.11f,beat*1.5);
            if(tick==0 || tick==9) for(int n=0;n<4;++n)
                note(Instrument::Keys,chord[n]+12,0.050f,beat*2.4,(n-1.5f)*0.37f);
            if(tick==6 || tick==14) note(Instrument::Pluck,chord[(bar+tick/4)%4]+24,0.042f,beat*1.4,-0.2f);
        } else {
            if(tick%4==0) note(Instrument::Kick,36,0.13f,0.34);
            if(tick==4 || tick==12) note(Instrument::Snare,50,0.073f,0.26,0.12f);
            if(tick%2==0) note(Instrument::Hat,90,0.030f,0.09,0.4f);
            if(tick%2==0) note(Instrument::Bass,chord[0]-12+(tick%4?12:0),0.10f,beat*0.42);
            if(tick==0) for(int n=0;n<4;++n)
                note(Instrument::Pad,chord[n]+12,0.036f,beat*4.05,(n-1.5f)*0.45f);
            if(tick%2==0) note(Instrument::Pluck,chord[(tick/2+(bar%2)*2)%4]+24,
                bar%4==3?0.060f:0.045f,beat*0.8f,tick%4?-0.45f:0.45f);
        }
        // Quiet fill variations at phrase boundaries are composed, not random samples.
        if(bar%4==3 && tick>=14) note(Instrument::Snare,52,0.023f,0.18,tick==14?-0.45f:0.45f);
        const double swing=station_==2?(tick%2?0.90:1.10):1.0;
        samplesToStep_+=sampleRate_*beat*0.25*swing;
        ++step_;
    }
};
}
