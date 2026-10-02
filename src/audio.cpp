#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "audio.h"
#include "audio_mailbox.h"
#include "audio_output_format.h"
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
}

struct Audio::Impl {
    std::mutex initialMutex;
    std::condition_variable initialCondition;
    AudioMailbox mailbox;
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
        unsigned validBits=bits;
        if(format.value->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&format.value->cbSize>=22) {
            const auto specified=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format.value)->Samples.wValidBitsPerSample;
            if(specified)validBits=specified;
        }
        const AudioOutputFormat outputFormat{format.value->nChannels,bits,validBits,floating};
        if((!floating && !isPcmFormat(format.value)) || !validAudioOutputFormat(outputFormat) ||
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
        AudioState latest;latest.station=0;latest.paused=true;
        bool primed=false;
        while(true) {
            const DWORD event=WaitForMultipleObjects(2,events,FALSE,2000);
            if(event==WAIT_OBJECT_0) {result=S_OK;break;}
            if(event==WAIT_FAILED) {result=HRESULT_FROM_WIN32(GetLastError());break;}
            UINT32 padding=0;
            result=client->GetCurrentPadding(&padding);
            if(FAILED(result)) break;
            if(padding>=capacity) continue;
            const UINT32 frames=capacity-padding;
            // A delayed publisher cannot block the device deadline. Reusing the
            // prior serial also lets spatial sources detect stale control data.
            const bool received=mailbox.tryRead(latest);
            if(received&&!primed){synth.prime(latest);primed=true;}
            else synth.update(latest);
            synth.render(samples.data(),frames);
            result=render->GetBuffer(frames,&buffer);
            if(FAILED(result)) break;
            if(!packAudioFrames(buffer,samples.data(),frames,outputFormat)) {
                render->ReleaseBuffer(frames,AUDCLNT_BUFFERFLAGS_SILENT);
                result=AUDCLNT_E_UNSUPPORTED_FORMAT;break;
            }
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
    impl->mailbox.publish(state);
}
}
