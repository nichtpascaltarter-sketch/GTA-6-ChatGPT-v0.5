// Compile with the same options against old/new Synth headers. The complete
// radio/player/world mix must be byte-identical when gunfire is absent.
#ifndef MC_SYNTH_HEADER
#define MC_SYNTH_HEADER "../src/synth.h"
#endif
#include MC_SYNTH_HEADER
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main(int argc,char** argv) {
    const bool raw=argc==2&&std::strcmp(argv[1],"--raw")==0;
    if(argc!=1&&!raw)return 2;
    std::uint64_t fingerprint=14695981039346656037ull,samples=0;
    std::array<float,256> buffer{};
    for(unsigned rate:{8000u,44100u,48000u,96000u,192000u,384000u}) {
        mc::Synth synth(rate);mc::AudioState state;state.world.epoch=1;
        state.world.engineCount=state.world.footCount=8;
        for(unsigned i=0;i<8;++i){state.world.engines[i]={10+i,.6f,.7f,20,.6f,int(i%4)};state.world.feet[i]={30+i,0,.5f,.8f,0,.8f,mc::FootSurface(i%5),int(i%2)};}
        for(unsigned step=0;step<240;++step) {
            state.station=int((step/30)%4);state.engine=float(step%20)/19;state.engineKind=int((step/20)%4);
            state.speed=float(step%100)-25;state.throttle=float(step%30)/29;state.rain=float(step%50)/49;
            state.wanted=float((step/25)%6);state.shot=step%13==0?1.f:0.f;state.shore=float(step%11)/10;
            state.nature=float(step%23)/22;state.urban=float(step%17)/16;state.volume=float(step%19)/18;
            state.footSurface=mc::FootSurface((step/10)%5);state.footContact=step%7!=0;state.footSpeed=float(step%9);
            state.waterMotion=float(step%13)/12;state.tireScrub=float(step%29)/28;state.paused=step>=140&&step<160;
            state.world.advancing=step<180||step>=190;++state.world.publicationSerial;
            if(step%20==0)for(auto& foot:state.world.feet)++foot.strikeSerial;
            if(step%48==24)for(unsigned i=0;i<4;++i){state.world.engines[i].id+=100;state.world.feet[i].id+=100;}
            if(step==110)++state.world.epoch;
            synth.update(state);
            unsigned remaining=rate/120;
            while(remaining) {
                const unsigned count=remaining>128?128:remaining;synth.render(buffer.data(),count);remaining-=count;
                if(raw&&std::fwrite(buffer.data(),sizeof(float),count*2,stdout)!=count*2)return 1;
                for(unsigned i=0;i<count*2;++i){fingerprint^=std::bit_cast<std::uint32_t>(buffer[i]);fingerprint*=1099511628211ull;++samples;}
            }
        }
    }
    std::fprintf(raw?stderr:stdout,"empty_gunfire_samples=%llu fingerprint=%016llx\n",static_cast<unsigned long long>(samples),static_cast<unsigned long long>(fingerprint));
}
