#include "../src/render_timing.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace mc;
namespace {
bool close(double a,double b){return std::abs(a-b)<1e-9;}
FrameTiming sample(uint64_t id){
    FrameTiming value;value.frameIndex=id;value.cpuRenderMs=double(id);
    value.cpuFenceWaitMs=double(id)*.1;value.cpuPrepareMs=double(id)*.2;
    value.cpuRecordSubmitMs=double(id)*.3;value.cpuPresentMs=double(id)*.4;
    value.gpuValid=(id%2)==0;value.gpuRenderMs=value.gpuValid?double(id)*2:99999;
    return value;
}
}
int main(){
    FrameRateWindow rate;rate.observe(0,0);rate.observe(0,.000001);
    assert(rate.framesPerSecond()==0);rate.observe(1,.8);
    assert(close(rate.framesPerSecond(),1.25));
    rate.reset();rate.observe(0,0);rate.observe(1,.01);rate.observe(2,.5);
    assert(close(rate.framesPerSecond(),4)); // Not the biased mean of 100 and 2.04.
    rate.reset();rate.observe(120,30);rate.observe(180,31);
    assert(close(rate.framesPerSecond(),60));
    rate.reset();rate.observe(180,100);rate.observe(210,100.5);
    assert(close(rate.framesPerSecond(),60)); // Inactive time is not rendered time.
    rate.observe(211,std::numeric_limits<double>::quiet_NaN());
    rate.observe(210,100.6);assert(close(rate.framesPerSecond(),60));
    rate.observe(0,101);assert(rate.framesPerSecond()==0);
    rate.observe(30,101.5);assert(close(rate.framesPerSecond(),60));
    rate.observe(30,0);assert(rate.framesPerSecond()==0);
    rate.observe(31,.8);assert(close(rate.framesPerSecond(),1.25));
    double ms=-1;
    assert(detail::timestampMilliseconds(123000,135345,1000000,ms)&&close(ms,12.345));
    assert(detail::timestampMilliseconds(7,7,1,ms)&&ms==0);
    assert(!detail::timestampMilliseconds(10,9,1000,ms)&&ms==0);
    assert(!detail::timestampMilliseconds(0,123,0,ms)&&ms==0);
    // Subtract integer ticks before converting: long-running high-frequency
    // clocks must preserve small intervals near the uint64_t limit.
    constexpr auto limit=std::numeric_limits<uint64_t>::max();
    assert(detail::timestampMilliseconds(limit-100,limit,1000000,ms)&&close(ms,.1));
    assert(detail::timestampMilliseconds(0,limit,1,ms)&&std::isfinite(ms)&&ms>0);

    detail::TimingHistory history;RenderTimingStats stats;
    history.summarize(stats);assert(stats.completedFrames==0&&stats.windowSamples==0&&stats.latest.frameIndex==0);
    history.append(sample(1));history.summarize(stats);
    assert(stats.completedFrames==1&&stats.windowSamples==1&&stats.windowGpuSamples==0);
    assert(stats.latest.frameIndex==1&&stats.mean.cpuRenderMs==1&&!stats.mean.gpuValid&&stats.mean.gpuRenderMs==0);
    history.append(sample(2));history.summarize(stats);
    assert(stats.windowSamples==2&&stats.windowGpuSamples==1&&stats.latest.frameIndex==2);
    assert(close(stats.mean.cpuRenderMs,1.5)&&close(stats.mean.gpuRenderMs,4)&&stats.mean.gpuValid);
    for(uint64_t id=3;id<=200;++id)history.append(sample(id));
    history.summarize(stats);
    assert(stats.completedFrames==200&&stats.windowSamples==120&&stats.windowGpuSamples==60);
    assert(stats.latest.frameIndex==200&&stats.maximum.frameIndex==0&&stats.mean.frameIndex==0);
    assert(close(stats.mean.cpuRenderMs,140.5)&&close(stats.maximum.cpuRenderMs,200));
    assert(close(stats.mean.cpuFenceWaitMs,14.05)&&close(stats.mean.cpuPrepareMs,28.1));
    assert(close(stats.mean.cpuRecordSubmitMs,42.15)&&close(stats.mean.cpuPresentMs,56.2));
    assert(close(stats.maximum.cpuFenceWaitMs,20)&&close(stats.maximum.cpuPrepareMs,40));
    assert(close(stats.maximum.cpuRecordSubmitMs,60)&&close(stats.maximum.cpuPresentMs,80));
    assert(close(stats.mean.gpuRenderMs,282)&&close(stats.maximum.gpuRenderMs,400));
    // Repeated polls do not count frames twice; CPU-only completions replace
    // older GPU entries without stale validity or a biased GPU denominator.
    history.summarize(stats);assert(stats.completedFrames==200&&stats.windowSamples==120);
    for(uint64_t id=201;id<=320;++id){auto value=sample(id);value.gpuValid=false;history.append(value);}
    history.summarize(stats);
    assert(stats.completedFrames==320&&stats.windowSamples==120&&stats.windowGpuSamples==0);
    assert(stats.latest.frameIndex==320&&!stats.mean.gpuValid&&!stats.maximum.gpuValid);
    assert(stats.mean.gpuRenderMs==0&&stats.maximum.gpuRenderMs==0&&close(stats.mean.cpuRenderMs,260.5));
    std::puts("Render timing: measured frame rates, startup/wake/reset intervals, timestamp units, invalid/reversed ticks, full-width precision, matched frame IDs, 120-frame wrap, valid-GPU mean, repeat polls, and CPU-only replacement passed.");
}
