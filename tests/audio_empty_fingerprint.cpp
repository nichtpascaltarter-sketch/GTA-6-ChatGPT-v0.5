// Compile this against both revisions with the same compiler/options to verify
// that adding the world mixer leaves every empty-world sample unchanged.
#include "../src/synth.h"
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
    for(unsigned rate:{8000u,44100u,48000u,96000u,384000u}) {
        mc::Synth synth(rate);mc::AudioState state;
        for(unsigned step=0;step<240;++step) {
            state.station=int((step/30)%4);state.engine=float(step%20)/19;state.engineKind=int((step/20)%4);
            state.speed=float(step%100)-25;state.throttle=float(step%30)/29;state.rain=float(step%50)/49;
            state.wanted=float((step/25)%6);state.shot=step%13==0?1.f:0.f;state.shore=float(step%11)/10;
            state.nature=float(step%23)/22;state.urban=float(step%17)/16;state.volume=float(step%19)/18;
            state.footSurface=mc::FootSurface((step/10)%5);state.footContact=step%7!=0;state.footSpeed=float(step%9);
            state.waterMotion=float(step%13)/12;state.tireScrub=float(step%29)/28;state.paused=step>=140&&step<160;
            synth.update(state);
            unsigned remaining=rate/120;
            while(remaining) {
                const unsigned count=remaining>128?128:remaining;synth.render(buffer.data(),count);remaining-=count;
                if(raw&&std::fwrite(buffer.data(),sizeof(float),count*2,stdout)!=count*2)return 1;
                for(unsigned i=0;i<count*2;++i){fingerprint^=std::bit_cast<std::uint32_t>(buffer[i]);fingerprint*=1099511628211ull;++samples;}
            }
        }
    }
    std::fprintf(raw?stderr:stdout,"empty_world_samples=%llu fingerprint=%016llx\n",static_cast<unsigned long long>(samples),static_cast<unsigned long long>(fingerprint));
}
