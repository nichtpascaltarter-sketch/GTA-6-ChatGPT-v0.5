#pragma once
#include "world.h"
#include <array>

namespace mc {
struct DialogueLine { const char* speaker; const char* text; float duration; };
class Cinematic {
public:
    void start(int chapter,Vec3 position,float heading) {
        if(chapter<0||chapter>=chapterCount()){chapter_=-1;return;}
        chapter_=chapter;anchor_=position;yaw_=heading;elapsed_=0;
    }
    bool active() const {return chapter_>=0;}
    int chapter() const {return chapter_;}
    static int chapterCount() {return int(lines().size());}
    float elapsed() const {return elapsed_;}
    void advance(float dt,bool skip=false) {
        if(!active())return;
        if(skip){chapter_=-1;return;}
        if(!std::isfinite(dt)||dt<0)return;
        elapsed_+=dt;
        float duration=0;for(const auto& line:lines()[size_t(chapter_)])duration+=line.duration;
        if(elapsed_>=duration)chapter_=-1;
    }
    const DialogueLine* dialogue() const {
        if(!active())return nullptr;
        float cursor=elapsed_;
        for(const auto& line:lines()[size_t(chapter_)]){if(cursor<line.duration)return &line;cursor-=line.duration;}
        return nullptr;
    }
    void camera(const World& world,Vec3& eye,Vec3& target) const {
        float shot=elapsed_,start=0;size_t index=0;
        for(const auto& line:lines()[size_t(std::max(chapter_,0))]){if(shot<line.duration)break;shot-=line.duration;start+=line.duration;++index;}
        index=std::min(index,size_t(2));
        float local=clamp((elapsed_-start)/lines()[size_t(std::max(chapter_,0))][index].duration,0,1);
        local=local*local*(3-2*local);
        const float angle=yaw_+(index==0?-.72f:index==1?1.15f:2.50f)+local*.16f;
        float distance=index==0?8.0f:index==1?4.2f:6.2f;
        target=anchor_+Vec3{0,index==0?1.9f:1.25f,0};
        Vec3 requested=target+forward(angle)*distance+Vec3{0,index==0?2.2f:.75f,0};
        // Pull a shot forward when authored scenery obstructs its line of sight.
        Vec3 delta=requested-target;float lengthToEye=length(delta);Vec3 direction=normalized(delta);eye=target;
        for(float t=.04f;t<lengthToEye+.04f;t+=.04f){
            Vec3 p=target+direction*std::min(t,lengthToEye);bool blocked=false;
            for(const auto& chunk:world.chunks)for(const auto& box:chunk.solids)
                if(p.x>box.min.x-.18f&&p.x<box.max.x+.18f&&p.y>box.min.y-.18f&&p.y<box.max.y+.18f&&p.z>box.min.z-.18f&&p.z<box.max.z+.18f)blocked=true;
            if(blocked)break;
            eye=p;
        }
        eye.y=std::max(eye.y,world.height(eye.x,eye.z)+.35f);
        if(length(eye-target)<.4f)target=eye-direction;
    }
private:
    int chapter_=-1;float elapsed_=0,yaw_=0;Vec3 anchor_;
    static const std::array<std::array<DialogueLine,3>,6>& lines() {
        static const std::array<std::array<DialogueLine,3>,6> script{{
            {{{"INEZ / PHONE","The tide is coming in. My car is still by the exchange. Bring it to the harbor steps.",6.5f},
              {"ROWAN","One delivery, then we're even. Why can't you collect it?",4.5f},
              {"INEZ / PHONE","Because somebody is watching the harbor. Keep your eyes on the road, Rowan.",6.0f}}},
            {{{"INEZ / PHONE","The clinic's backup power died this morning. Its replacement batteries never made it past the arcade.",7.0f},
              {"ROWAN","A missing shipment. And now the harbor has its own private patrols. That's convenient.",6.0f},
              {"INEZ / PHONE","Ask questions later. Pick up the batteries and find Mara in Westhaven before the lights go out.",6.5f}}},
            {{{"MARA / PHONE","These records explain the new harbor fees. Every payment leads back to the same private account.",7.0f},
              {"ROWAN","If I take them south, will you have someone who can make them public?",5.0f},
              {"MARA / PHONE","Yes. But don't bring a patrol to our meeting. Lose them first. I'll wait as long as I can.",6.5f}}},
            {{{"MARA / PHONE","Our witness left two recordings on the eastern promenade. The account numbers are only half the story.",7.0f},
              {"ROWAN","And the other half?",3.0f},
              {"MARA / PHONE","Names. Get both recordings. Once they're on the air, this city can decide who it belongs to.",6.5f}}},
            {{{"MARA / RADIO","Leena's clinic launch lost power out past Glasswater. She's still aboard, but the running lights are fading.",7.5f},
              {"ROWAN","I'll take the runabout. Tell her to keep a light on. I'll find her.",5.0f},
              {"MARA / RADIO","Come alongside slowly and hold there while she crosses. Bring her back to this pier. We don't leave anyone out there.",7.5f}}},
            {{{"INEZ / RADIO","Leena gave Mara a radio log. Three survey passes will locate the unlisted transmitters behind those harbor shipments.",7.5f},
              {"ROWAN","Mark the passes and the safe heights. I'll take the survey plane up.",5.0f},
              {"INEZ / RADIO","Fly each height band. Ease off the throttle for the turns, then bring the recorder back to Breaker with a gentle landing.",7.5f}}}
        }};
        return script;
    }
};
}
