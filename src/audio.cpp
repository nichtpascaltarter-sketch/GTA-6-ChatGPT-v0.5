#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "audio.h"
#include "synth.h"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace mc {
namespace {
template<class T> class ComPtr {
public:
    ~ComPtr() { if(value_) value_->Release(); }
    T* operator->() const {return value_;}
    T** put() {return &value_;}
    T* get() const {return value_;}
    ComPtr()=default;
    ComPtr(const ComPtr&)=delete;
    ComPtr& operator=(const ComPtr&)=delete;
private:
    T* value_=nullptr;
};
struct Format {
    WAVEFORMATEX* value=nullptr;
    ~Format() {CoTaskMemFree(value);}
};
std::string failure(const char* stage,HRESULT result) {
    char code[32]{};
    std::snprintf(code,sizeof(code)," (0x%08lX)",static_cast<unsigned long>(result));
    return std::string(stage)+code;
}
bool isFloatFormat(const WAVEFORMATEX* wave) {
    if(wave->wFormatTag==WAVE_FORMAT_IEEE_FLOAT) return true;
    if(wave->wFormatTag!=WAVE_FORMAT_EXTENSIBLE || wave->cbSize<22) return false;
    constexpr GUID floatGuid={3,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
    return IsEqualGUID(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave)->SubFormat,floatGuid)!=0;
}
bool isPcmFormat(const WAVEFORMATEX* wave) {
    if(wave->wFormatTag==WAVE_FORMAT_PCM) return true;
    if(wave->wFormatTag!=WAVE_FORMAT_EXTENSIBLE || wave->cbSize<22) return false;
    constexpr GUID pcmGuid={1,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
    return IsEqualGUID(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave)->SubFormat,pcmGuid)!=0;
}
void interleave(BYTE* output,const float* stereo,UINT32 frames,const WAVEFORMATEX* format,bool floating) {
    const unsigned bytes=format->wBitsPerSample/8;
    unsigned validBits=format->wBitsPerSample;
    if(format->wFormatTag==WAVE_FORMAT_EXTENSIBLE && format->cbSize>=22) {
        const unsigned specified=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->Samples.wValidBitsPerSample;
        if(specified>0 && specified<validBits) validBits=specified;
    }
    for(UINT32 i=0;i<frames;++i) for(unsigned channel=0;channel<format->nChannels;++channel) {
        float value=0;
        if(format->nChannels==1) value=(stereo[i*2]+stereo[i*2+1])*0.5f;
        else if(channel<2) value=stereo[i*2+channel];
        value=std::max(-1.0f,std::min(1.0f,value));
        BYTE* destination=output+static_cast<std::size_t>(i)*format->nBlockAlign+channel*bytes;
        if(floating) {std::memcpy(destination,&value,sizeof(value));continue;}
        if(bytes==1) {*destination=static_cast<BYTE>((value+1.0f)*127.5f);continue;}
        const double maximum=bytes==2?32767.0:(bytes==3?8388607.0:2147483647.0);
        const auto integer=static_cast<std::int32_t>(value*maximum);
        auto bits=static_cast<std::uint32_t>(integer);
        // Extensible PCM stores valid samples left-aligned in their containers.
        const unsigned padding=format->wBitsPerSample-validBits;
        if(padding>0) bits&=0xffffffffu<<padding;
        for(unsigned b=0;b<bytes;++b) destination[b]=static_cast<BYTE>(bits>>(b*8));
    }
}
}

struct Audio::Impl {
    std::mutex stateMutex,initialMutex;
    std::condition_variable initialCondition;
    AudioState state{};
    std::thread worker;
    HANDLE stopEvent=nullptr,sampleEvent=nullptr;
    bool initialized=false,success=false;
    std::string initialError;
    std::atomic<bool> running{false};

    ~Impl() {stop();}
    void stop() {
        if(stopEvent) SetEvent(stopEvent);
        if(worker.joinable()) worker.join();
        if(sampleEvent) CloseHandle(sampleEvent);
        if(stopEvent) CloseHandle(stopEvent);
        sampleEvent=stopEvent=nullptr;running=false;
    }
    void report(bool okay,const std::string& error) {
        std::lock_guard<std::mutex> guard(initialMutex);
        if(initialized) return;
        initialized=true;success=okay;initialError=error;
        initialCondition.notify_one();
    }
    HRESULT stream(std::string& error) {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(enumerator.put()));
        if(FAILED(result)) {error=failure("Cannot create the Windows audio device enumerator",result);return result;}
        ComPtr<IMMDevice> device;
        result=enumerator->GetDefaultAudioEndpoint(eRender,eConsole,device.put());
        if(FAILED(result)) {error=failure("No Windows playback device is available",result);return result;}
        ComPtr<IAudioClient> client;
        result=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.put()));
        if(FAILED(result)) {error=failure("Cannot activate the Windows playback device",result);return result;}
        Format format;
        result=client->GetMixFormat(&format.value);
        if(FAILED(result)) {error=failure("Cannot read the Windows playback format",result);return result;}
        const bool floating=isFloatFormat(format.value);
        const unsigned bits=format.value->wBitsPerSample;
        if((!floating && !isPcmFormat(format.value)) || (floating && bits!=32) ||
            (!floating && bits!=8 && bits!=16 && bits!=24 && bits!=32) ||
            format.value->nChannels<1 || format.value->nChannels>32 ||
            format.value->nSamplesPerSec<8000 || format.value->nSamplesPerSec>384000 ||
            format.value->nBlockAlign!=format.value->nChannels*(bits/8)) {
            error="The Windows playback device reported an unsupported sample format";
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        result=client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_NOPERSIST,
            1000000,0,format.value,nullptr);
        if(FAILED(result)) {error=failure("Cannot initialize shared Windows audio",result);return result;}
        ResetEvent(sampleEvent);
        result=client->SetEventHandle(sampleEvent);
        if(FAILED(result)) {error=failure("Cannot register the audio buffer event",result);return result;}
        UINT32 capacity=0;
        result=client->GetBufferSize(&capacity);
        if(FAILED(result) || capacity==0) {error=failure("Cannot read the audio buffer size",result);return E_FAIL;}
        ComPtr<IAudioRenderClient> render;
        result=client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(render.put()));
        if(FAILED(result)) {error=failure("Cannot create the audio render stream",result);return result;}
        std::vector<float> samples(static_cast<std::size_t>(capacity)*2);
        Synth synth(format.value->nSamplesPerSec);
        BYTE* buffer=nullptr;
        result=render->GetBuffer(capacity,&buffer);
        if(FAILED(result)) {error=failure("Cannot acquire the initial audio buffer",result);return result;}
        result=render->ReleaseBuffer(capacity,AUDCLNT_BUFFERFLAGS_SILENT);
        if(FAILED(result)) {error=failure("Cannot prepare the audio buffer",result);return result;}
        result=client->Start();
        if(FAILED(result)) {error=failure("Cannot start Windows audio",result);return result;}
        running=true;
        report(true,{});
        HANDLE events[]={stopEvent,sampleEvent};
        while(true) {
            const DWORD event=WaitForMultipleObjects(2,events,FALSE,2000);
            if(event==WAIT_OBJECT_0) {result=S_OK;break;}
            if(event==WAIT_FAILED) {result=HRESULT_FROM_WIN32(GetLastError());break;}
            UINT32 padding=0;
            result=client->GetCurrentPadding(&padding);
            if(FAILED(result)) break;
            if(padding>=capacity) continue;
            const UINT32 frames=capacity-padding;
            AudioState latest;
            {std::lock_guard<std::mutex> guard(stateMutex);latest=state;}
            synth.update(latest);
            synth.render(samples.data(),frames);
            result=render->GetBuffer(frames,&buffer);
            if(FAILED(result)) break;
            interleave(buffer,samples.data(),frames,format.value,floating);
            result=render->ReleaseBuffer(frames,0);
            if(FAILED(result)) break;
        }
        client->Stop();
        running=false;
        return result;
    }
    void run() noexcept {
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(com)) {report(false,failure("Cannot initialize audio COM",com));return;}
        try {
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
            bool first=true;
            while(WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0) {
                std::string error;
                const HRESULT result=stream(error);
                if(first && !success) {report(false,error);break;}
                first=false;
                if(SUCCEEDED(result)) break;
                // A removed or reconfigured playback endpoint is reopened automatically.
                if(WaitForSingleObject(stopEvent,1000)==WAIT_OBJECT_0) break;
            }
        } catch(const std::exception& exception) {
            report(false,std::string("Cannot allocate the Windows audio stream: ")+exception.what());
        } catch(...) {report(false,"Windows audio stream initialization failed");}
        running=false;
        CoUninitialize();
    }
};

Audio::Audio():impl(std::make_unique<Impl>()) {}
Audio::~Audio()=default;
bool Audio::initialize(std::string& error) {
    error.clear();
    if(impl->running) return true;
    impl->stop();
    impl->initialized=false;impl->success=false;impl->initialError.clear();
    impl->stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    impl->sampleEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!impl->stopEvent || !impl->sampleEvent) {
        error=failure("Cannot create the Windows audio synchronization events",HRESULT_FROM_WIN32(GetLastError()));
        impl->stop();return false;
    }
    try {impl->worker=std::thread([this]{impl->run();});}
    catch(const std::exception& exception) {error=std::string("Cannot create the audio thread: ")+exception.what();impl->stop();return false;}
    std::unique_lock<std::mutex> lock(impl->initialMutex);
    impl->initialCondition.wait(lock,[this]{return impl->initialized;});
    const bool okay=impl->success;
    error=impl->initialError;
    lock.unlock();
    if(!okay) impl->stop();
    return okay;
}
void Audio::update(const AudioState& state) {
    std::lock_guard<std::mutex> guard(impl->stateMutex);
    impl->state=state;
}
}
