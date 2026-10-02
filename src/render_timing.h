#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mc {
// Count completed frames over elapsed wall time. Averaging reciprocal frame
// intervals biases the result upward and lets a tiny startup interval dominate.
class FrameRateWindow {
    uint64_t firstFrame=0;
    double firstTime=0;
    float rate=0;
    bool primed=false;
public:
    void reset(){*this={};}
    void observe(uint64_t frames,double seconds){
        if(!std::isfinite(seconds))return;
        if(!primed||frames<firstFrame||seconds<firstTime){
            firstFrame=frames;firstTime=seconds;rate=0;primed=true;return;
        }
        const double elapsed=seconds-firstTime;
        if(elapsed>=.5&&frames>firstFrame){
            rate=float(double(frames-firstFrame)/elapsed);
            firstFrame=frames;firstTime=seconds;
        }
    }
    float framesPerSecond() const {return rate;}
};
struct FrameTiming {
    uint64_t frameIndex=0;
    // Wall time within Renderer::render only. Preparation includes transient
    // uploads/constants; recording includes queue submission and fence signal.
    // Total also includes small bookkeeping and optional debug validation costs.
    double cpuRenderMs=0,cpuFenceWaitMs=0,cpuPrepareMs=0,cpuRecordSubmitMs=0,cpuPresentMs=0;
    // Elapsed direct-queue interval: shadow through final PRESENT transition.
    // Excludes separate world upload/AS lists, Present/vsync and screenshots.
    double gpuRenderMs=0;bool gpuValid=false;
};
struct RenderTimingStats {
    bool cpuAvailable=false,gpuAvailable=false;
    uint64_t gpuTimestampFrequency=0,submittedFrames=0,completedFrames=0,invalidGpuSamples=0;
    uint32_t pendingFrames=0,windowSamples=0,windowGpuSamples=0;
    // Individual samples pair CPU/GPU values for the same completed frame.
    // Latest may lag by two frames; mean/maximum are fieldwise aggregates over
    // up to 120 completions. Aggregate frameIndex is zero; GPU aggregates use
    // valid samples only (windowGpuSamples records their denominator).
    FrameTiming latest,mean,maximum;
    // All setWorld attempts, including unchanged views and pressure waits.
    uint64_t worldCalls=0;double worldLastMs=0,worldTotalMs=0,worldMaxMs=0;
};
namespace detail {
inline bool timestampMilliseconds(uint64_t begin,uint64_t end,uint64_t frequency,double& result){
    result=0;if(!frequency||end<begin)return false;
    result=double(end-begin)*(1000.0/double(frequency));
    return std::isfinite(result)&&result>=0;
}
class TimingHistory {
    std::array<FrameTiming,120> samples{};
    uint32_t next=0,count=0;
    uint64_t completions=0;
public:
    void append(const FrameTiming& sample){
        samples[next]=sample;next=(next+1)%uint32_t(samples.size());
        count=std::min(count+1,uint32_t(samples.size()));++completions;
    }
    void summarize(RenderTimingStats& result)const{
        result.windowSamples=count;result.completedFrames=completions;
        result.windowGpuSamples=0;result.latest={};result.mean={};result.maximum={};
        if(!count)return;
        result.latest=samples[(next+uint32_t(samples.size())-1)%uint32_t(samples.size())];
        constexpr std::array<double FrameTiming::*,5> cpuFields{
            &FrameTiming::cpuRenderMs,&FrameTiming::cpuFenceWaitMs,&FrameTiming::cpuPrepareMs,
            &FrameTiming::cpuRecordSubmitMs,&FrameTiming::cpuPresentMs};
        for(uint32_t i=0;i<count;++i){
            const auto& sample=samples[i];
            for(auto member:cpuFields){result.mean.*member+=sample.*member;result.maximum.*member=std::max(result.maximum.*member,sample.*member);}
            if(sample.gpuValid){++result.windowGpuSamples;result.mean.gpuRenderMs+=sample.gpuRenderMs;result.maximum.gpuRenderMs=std::max(result.maximum.gpuRenderMs,sample.gpuRenderMs);}
        }
        for(auto member:cpuFields)result.mean.*member/=count;
        if(result.windowGpuSamples){result.mean.gpuRenderMs/=result.windowGpuSamples;result.mean.gpuValid=result.maximum.gpuValid=true;}
    }
};
}
}
