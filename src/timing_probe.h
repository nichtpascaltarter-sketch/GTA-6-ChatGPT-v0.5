#pragma once
#include "render_timing.h"
#include <iomanip>
#include <ostream>
#include <string>

namespace mc {
class TimingProbe {
public:
    bool observe(const RenderTimingStats& t,uint64_t frames,std::string& error) {
        if(t.submittedFrames!=frames||t.completedFrames<completed_||t.completedFrames>frames||
           t.pendingFrames>2||t.completedFrames+t.pendingFrames!=frames||
           t.windowSamples!=std::min<uint64_t>(120,t.completedFrames)||t.windowGpuSamples>t.windowSamples||
           t.latest.frameIndex<latest_||t.latest.frameIndex!=t.completedFrames) {
            error="Render timing diagnostic found a missing, reordered or duplicated frame sample.";return false;
        }
        const auto finite=[](double v){return std::isfinite(v)&&v>=0;};
        const auto sampleValid=[&](const FrameTiming& v){
            return finite(v.cpuRenderMs)&&finite(v.cpuFenceWaitMs)&&finite(v.cpuPrepareMs)&&
                finite(v.cpuRecordSubmitMs)&&finite(v.cpuPresentMs)&&finite(v.gpuRenderMs);
        };
        if(!sampleValid(t.latest)||!sampleValid(t.mean)||!sampleValid(t.maximum)||
           !finite(t.worldLastMs)||!finite(t.worldTotalMs)||!finite(t.worldMaxMs)||!t.worldCalls||
           t.invalidGpuSamples||(!t.gpuAvailable&&(t.windowGpuSamples||t.gpuTimestampFrequency))||
           (t.gpuAvailable&&(!t.gpuTimestampFrequency||t.windowGpuSamples!=t.windowSamples))){
            error="Render timing diagnostic found invalid timer values or inconsistent availability.";return false;
        }
        const double parts=t.latest.cpuFenceWaitMs+t.latest.cpuPrepareMs+t.latest.cpuRecordSubmitMs+t.latest.cpuPresentMs;
        if(parts>t.latest.cpuRenderMs+.01||(t.windowGpuSamples&&t.mean.gpuRenderMs<=0)){
            error="Render timing diagnostic found overlapping CPU phases or an empty GPU interval.";return false;
        }
        completed_=t.completedFrames;latest_=t.latest.frameIndex;return true;
    }
    static void write(std::ostream& log,const RenderTimingStats& t) {
        const auto flags=log.flags();const auto precision=log.precision();
        log<<std::fixed<<std::setprecision(6)
           <<"Render timing: cpu="<<t.cpuAvailable<<"; gpu="<<t.gpuAvailable<<"; frequency="<<t.gpuTimestampFrequency
           <<"; submitted="<<t.submittedFrames<<"; completed="<<t.completedFrames<<"; pending="<<t.pendingFrames
           <<"; samples="<<t.windowSamples<<"; gpuSamples="<<t.windowGpuSamples<<"; invalid="<<t.invalidGpuSamples
           <<"; latest="<<t.latest.frameIndex<<"; cpuMean="<<t.mean.cpuRenderMs<<"; cpuMax="<<t.maximum.cpuRenderMs
           <<"; waitMean="<<t.mean.cpuFenceWaitMs<<"; prepareMean="<<t.mean.cpuPrepareMs<<"; recordMean="<<t.mean.cpuRecordSubmitMs
           <<"; presentMean="<<t.mean.cpuPresentMs<<"; gpuMean="<<t.mean.gpuRenderMs<<"; gpuMax="<<t.maximum.gpuRenderMs
           <<"; worldCalls="<<t.worldCalls<<"; worldMean="<<(t.worldCalls?t.worldTotalMs/double(t.worldCalls):0)
           <<"; worldMax="<<t.worldMaxMs<<'\n';
        log.flags(flags);log.precision(precision);log.flush();
    }
private:
    uint64_t completed_=0,latest_=0;
};
}
