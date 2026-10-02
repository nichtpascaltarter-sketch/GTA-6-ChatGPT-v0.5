#include "renderer.h"
#include "render_visibility.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#if defined(MC_DEBUG) && MC_DEBUG
#include <d3d12sdklayers.h>
#endif
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <climits>
#include <cstdio>
#include <algorithm>
#include <map>
#include <deque>
#include "world_vs.h"
#include "shadow_vs.h"
#include "world_ps.h"
#include "world_far_ps.h"
#include "world_rt_ps.h"
#include "sky_vs.h"
#include "sky_ps.h"
#include "ui_vs.h"
#include "ui_ps.h"
#include "post_ps.h"

namespace mc {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT FrameCount=2;
constexpr UINT ShadowSize=2048;
constexpr UINT MaxLights=64;
constexpr uint64_t MiB=1024*1024;
constexpr uint64_t DefaultVertexBudget=64*MiB,DefaultIndexBudget=16*MiB;
constexpr uint64_t MaxVertexBudget=256*MiB,MaxIndexBudget=64*MiB;
constexpr uint64_t MaxStagingBytes=384*MiB;
constexpr size_t MaxUploadBatches=4;
constexpr float ShadowSpan=240.0f;
constexpr float SceneNear=.12f,SceneFar=3000.0f;
constexpr DXGI_FORMAT ColorFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT SceneFormat=DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT DepthFormat=DXGI_FORMAT_D32_FLOAT;
struct Constants {
    Mat4 viewProjection;
    float eyeTime[4],sunDay[4],weather[4],cameraRight[4],cameraUp[4],cameraForward[4],viewport[4];
    Mat4 lightProjection;
    float visibility[4];
};
static_assert(sizeof(Constants)==256,"Frame constants must fit one aligned allocation");
static_assert(sizeof(Light)==48,"Shader light layout mismatch");
static_assert(sizeof(Vertex)==40,"Shader vertex layout mismatch");
static_assert(sizeof(UiVertex)==24,"UI vertex layout mismatch");
bool checked(HRESULT hr,const char* operation,std::string& error) {
    if(SUCCEEDED(hr))return true;
    char code[48]{};std::snprintf(code,sizeof(code)," (HRESULT 0x%08lX)",static_cast<unsigned long>(hr));
    error=operation;error+=code;
    char* message=nullptr;
    if(FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,DWORD(hr),0,reinterpret_cast<char*>(&message),0,nullptr)){
        error+=" ";error+=message;LocalFree(message);
    }
    return false;
}
D3D12_RESOURCE_DESC bufferDesc(uint64_t bytes,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=std::max<uint64_t>(bytes,256);d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;return d;
}
D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES p{};p.Type=type;p.CreationNodeMask=1;p.VisibleNodeMask=1;return p;}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=resource;b.Transition.StateBefore=before;b.Transition.StateAfter=after;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;list->ResourceBarrier(1,&b);
}
void uavBarrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;list->ResourceBarrier(1,&b);}
// Element offsets avoid the invalid byte alignment produced by power-of-two
// allocation for the 40-byte vertex stride. Released intervals are coalesced.
struct ArenaRange {uint32_t first=0,count=0;};
struct ArenaAllocator {
    std::vector<ArenaRange> free;
    void reset(uint32_t capacity){free={{0,capacity}};}
    bool allocate(uint32_t count,ArenaRange& result){
        result={0,count};if(!count)return true;
        size_t best=free.size();
        for(size_t i=0;i<free.size();++i)if(free[i].count>=count&&(best==free.size()||free[i].count<free[best].count))best=i;
        if(best==free.size())return false;
        result.first=free[best].first;free[best].first+=count;free[best].count-=count;
        if(!free[best].count)free.erase(free.begin()+best);return true;
    }
    void release(ArenaRange range){
        if(!range.count)return;
        auto at=std::lower_bound(free.begin(),free.end(),range.first,[](ArenaRange a,uint32_t first){return a.first<first;});
        size_t index=size_t(at-free.begin());free.insert(at,range);
        if(index&&free[index-1].first+free[index-1].count==free[index].first){free[index-1].count+=free[index].count;free.erase(free.begin()+index);--index;}
        if(index+1<free.size()&&free[index].first+free[index].count==free[index+1].first){free[index].count+=free[index+1].count;free.erase(free.begin()+index+1);}
    }
};
}
struct Renderer::Impl {
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> constants,dynamicVertices,dynamicIndices,ui,lights;
        uint8_t* mappedConstants=nullptr;
        uint64_t fence=0,dynamicVertexCapacity=0,dynamicIndexCapacity=0,uiCapacity=0;
        FrameTiming timing;bool timingPending=false;
    };
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12Device5> device5;
#if defined(MC_DEBUG) && MC_DEBUG
    ComPtr<ID3D12InfoQueue> debugMessages;
#endif
    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12GraphicsCommandList4> commands4;
    ComPtr<ID3D12Fence> fence;HANDLE fenceEvent=nullptr;uint64_t nextFence=1,totalFrames=0;
    std::array<Frame,FrameCount> frames;
    ComPtr<ID3D12QueryHeap> timestampHeap;
    ComPtr<ID3D12Resource> timestampReadback;
    const uint64_t* mappedTimestamps=nullptr;
    uint64_t cpuFrequency=0;
    RenderTimingStats timing;
    detail::TimingHistory timingHistory;
    ComPtr<ID3D12DescriptorHeap> rtvHeap,dsvHeap,resourceHeap;
    std::array<ComPtr<ID3D12Resource>,FrameCount> backBuffers;ComPtr<ID3D12Resource> depth,shadowDepth,sceneColor,resolvedColor;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> worldPipeline,farPipeline,rayPipeline,skyPipeline,uiPipeline,shadowPipeline,postPipeline;
    struct ResidentChunk {
        ArenaRange vertices,indices;
        RenderTileKey key;Box bounds;
        ComPtr<ID3D12Resource> blas;
        uint64_t blasBytes=0;
        uint64_t geometryBytes()const{return uint64_t(vertices.count)*sizeof(Vertex)+uint64_t(indices.count)*sizeof(uint32_t);}
    };
    struct ActiveTile {std::shared_ptr<ResidentChunk> chunk;bool rayTrace=false,shadowCaster=false;};
    using ChunkKey=RenderTileKey;
    using ChunkMap=std::map<ChunkKey,std::shared_ptr<ResidentChunk>>;
    struct RetiredChunk {uint64_t fence=0;std::shared_ptr<ResidentChunk> chunk;};
    struct RetiredScene {uint64_t fence=0;ComPtr<ID3D12Resource> tlas,metadata;};
    struct UploadBatch {
        uint64_t fence=0,stagingBytes=0;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12GraphicsCommandList4> rayCommands;
        ComPtr<ID3D12Resource> vertices,indices,instances,scratch;
        // These references also protect a submitted batch if queue Signal fails.
        ComPtr<ID3D12Resource> arenaVertices,arenaIndices,tlas,metadata;
        std::vector<std::shared_ptr<ResidentChunk>> chunks;
    };
    ComPtr<ID3D12Resource> worldVertices,worldIndices,tlas,instanceMetadata;
    D3D12_VERTEX_BUFFER_VIEW worldVB{};D3D12_INDEX_BUFFER_VIEW worldIB{};
    ArenaAllocator vertexAllocator,indexAllocator;
    ChunkMap resident;
    std::vector<ActiveTile> activeChunks;
    std::vector<std::shared_ptr<ResidentChunk>> rayResidents;
    std::deque<RetiredChunk> retiredChunks;
    std::deque<RetiredScene> retiredScenes;
    std::deque<UploadBatch> uploadBatches;
    StreamStats streaming;
    uint64_t worldRevision=0;bool worldPublished=false;
    UINT width=0,height=0,rtvStride=0,srvStride=0,sceneSamples=1,lastPresented=0;bool tearing=false,raySupported=false,hasPresented=false;
    std::string gpuName="Unavailable";
    ~Impl(){std::string ignored;if(queue&&fence&&fenceEvent)flush(ignored);for(auto& f:frames)if(f.constants&&f.mappedConstants)f.constants->Unmap(0,nullptr);if(mappedTimestamps){D3D12_RANGE written{0,0};timestampReadback->Unmap(0,&written);}if(fenceEvent)CloseHandle(fenceEvent);}
    uint64_t cpuTick()const{
        LARGE_INTEGER value{};return cpuFrequency&&QueryPerformanceCounter(&value)&&value.QuadPart>=0?uint64_t(value.QuadPart):0;
    }
    double cpuElapsed(uint64_t begin,uint64_t end)const{
        double ms=0;if(begin&&end)detail::timestampMilliseconds(begin,end,cpuFrequency,ms);return ms;
    }
    void initializeTiming(){
        LARGE_INTEGER frequency{};
        if(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0)cpuFrequency=uint64_t(frequency.QuadPart);
        timing.cpuAvailable=cpuFrequency!=0;
        std::fprintf(stderr,"Renderer CPU timings: %s; scope=Renderer::render; setWorld=separate\n",timing.cpuAvailable?"enabled":"unavailable");
        char setting[8]{};
        if(GetEnvironmentVariableA("MERIDIAN_GPU_TIMESTAMPS",setting,sizeof(setting))==1&&setting[0]=='0'){
            std::fputs("GPU frame timestamps: disabled by MERIDIAN_GPU_TIMESTAMPS=0\n",stderr);return;
        }
        // Direct queues support timestamp queries. Frequency discovery and all
        // optional resource creation may nevertheless fail; never block launch.
        uint64_t gpuFrequency=0;std::string reason;
        D3D12_QUERY_HEAP_DESC desc{};desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;desc.Count=FrameCount*2;
        bool available=checked(queue->GetTimestampFrequency(&gpuFrequency),"Get direct-queue timestamp frequency",reason)&&gpuFrequency!=0;
        if(available)available=checked(device->CreateQueryHeap(&desc,IID_PPV_ARGS(&timestampHeap)),"Create frame timestamp heap",reason);
        if(available)available=createBuffer(FrameCount*2*sizeof(uint64_t),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,timestampReadback,reason);
        if(available){
            void* mapped=nullptr;D3D12_RANGE read{0,FrameCount*2*sizeof(uint64_t)};
            available=checked(timestampReadback->Map(0,&read,&mapped),"Map frame timestamps",reason);
            if(available)mappedTimestamps=static_cast<const uint64_t*>(mapped);
        }
        if(!available){
            timestampReadback.Reset();timestampHeap.Reset();
            // FormatMessage may append CR/LF; keep this machine-readable
            // optional-capability status on one line in native smoke logs.
            for(char& c:reason)if(c=='\r'||c=='\n')c=' ';
            std::fprintf(stderr,"GPU frame timestamps: unavailable (%s)\n",reason.empty()?"zero queue timestamp frequency":reason.c_str());return;
        }
        timing.gpuAvailable=true;timing.gpuTimestampFrequency=gpuFrequency;
        std::fprintf(stderr,"GPU frame timestamps: enabled; frequency=%llu Hz; slots=%u; history=120\n",static_cast<unsigned long long>(gpuFrequency),FrameCount);
        std::fputs("GPU timing scope: shadow through final PRESENT transition, including scene, HDR resolve, post and UI; excludes world upload/AS lists, queue backlog, Present/vsync, capture and timestamp resolve.\n",stderr);
    }
    void collectTimings(){
        if(!fence)return;
        const uint64_t completed=fence->GetCompletedValue();
        if(completed==UINT64_MAX)return; // Device removed: readback is not safe.
        // Swap-chain indices need not arrive in index order. Publish completed
        // pairs by frame ID so latest and the bounded window stay chronological.
        for(UINT processed=0;processed<FrameCount;++processed){
            UINT oldest=FrameCount;
            for(UINT i=0;i<FrameCount;++i)if(frames[i].timingPending&&frames[i].fence<=completed&&
                (oldest==FrameCount||frames[i].timing.frameIndex<frames[oldest].timing.frameIndex))oldest=i;
            if(oldest==FrameCount)break;
            auto& frame=frames[oldest];
            if(timing.gpuAvailable){
                frame.timing.gpuValid=detail::timestampMilliseconds(mappedTimestamps[oldest*2],mappedTimestamps[oldest*2+1],timing.gpuTimestampFrequency,frame.timing.gpuRenderMs);
                if(!frame.timing.gpuValid)++timing.invalidGpuSamples;
            }
            timingHistory.append(frame.timing);frame.timingPending=false;
        }
    }
    RenderTimingStats timingStats(){
        collectTimings();RenderTimingStats result=timing;timingHistory.summarize(result);
        for(const auto& frame:frames)result.pendingFrames+=frame.timingPending?1u:0u;
        return result;
    }
    bool checkDebugMessages(std::string& error){
#if defined(MC_DEBUG) && MC_DEBUG
        if(!debugMessages)return true;
        const UINT64 count=debugMessages->GetNumStoredMessagesAllowedByRetrievalFilter();
        unsigned errors=0;
        for(UINT64 i=0;i<count;++i){
            SIZE_T bytes=0;
            if(FAILED(debugMessages->GetMessage(i,nullptr,&bytes))||!bytes)continue;
            std::vector<uint8_t> storage(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(FAILED(debugMessages->GetMessage(i,message,&bytes)))continue;
            if(message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION&&message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR)continue;
            if(errors++==0)error="Direct3D 12 debug validation failed:";
            if(errors<=8){
                error+="\n["+std::to_string(static_cast<unsigned>(message->ID))+"] ";
                if(message->pDescription){
                    SIZE_T length=message->DescriptionByteLength;
                    while(length&&message->pDescription[length-1]=='\0')--length;
                    error.append(message->pDescription,length);
                }
            }
        }
        debugMessages->ClearStoredMessages();
        if(errors>8)error+="\nAdditional validation errors: "+std::to_string(errors-8);
        return errors==0;
#else
        static_cast<void>(error);return true;
#endif
    }
    bool wait(uint64_t value,std::string& error){
        const uint64_t complete=fence->GetCompletedValue();
        if(complete==UINT64_MAX)return checked(device->GetDeviceRemovedReason(),"Device removed while waiting for GPU",error);
        if(!value||complete>=value)return true;
        if(!checked(fence->SetEventOnCompletion(value,fenceEvent),"Schedule GPU fence",error))return false;
        DWORD status=WaitForSingleObject(fenceEvent,15000);
        if(status!=WAIT_OBJECT_0){error="GPU fence did not complete within 15 seconds";if(status==WAIT_FAILED)checked(HRESULT_FROM_WIN32(GetLastError()),"Wait for GPU fence",error);return false;}return true;
    }
    bool flush(std::string& error){const uint64_t value=nextFence++;if(!checked(queue->Signal(fence.Get(),value),"Signal GPU fence",error)||!wait(value,error))return false;collectTimings();return true;}
    bool createBuffer(uint64_t bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,ComPtr<ID3D12Resource>& result,std::string& error,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
        auto desc=bufferDesc(bytes,flags);auto properties=heapProperties(heap);return checked(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"Allocate GPU buffer",error);
    }
    bool writeUpload(ID3D12Resource* resource,const void* data,size_t size,std::string& error){
        if(!size)return true;void* mapped=nullptr;D3D12_RANGE noRead{0,0};if(!checked(resource->Map(0,&noRead,&mapped),"Map upload buffer",error))return false;
        std::memcpy(mapped,data,size);D3D12_RANGE written{0,size};resource->Unmap(0,&written);return true;
    }
    bool ensureUpload(ComPtr<ID3D12Resource>& resource,uint64_t& capacity,uint64_t bytes,std::string& error){
        if(bytes<=capacity)return true;resource.Reset();capacity=0;
        uint64_t target=std::max<uint64_t>(bytes,4096);target=(target+65535)&~uint64_t(65535);
        if(!createBuffer(target,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,resource,error))return false;capacity=target;return true;
    }
    bool beginImmediate(std::string& error){return flush(error)&&checked(frames[0].allocator->Reset(),"Reset upload allocator",error)&&checked(commands->Reset(frames[0].allocator.Get(),nullptr),"Reset upload command list",error);}
    bool endImmediate(std::string& error){if(!checked(commands->Close(),"Close upload command list",error))return false;ID3D12CommandList* lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);return flush(error);}
    bool selectSceneSamples(std::string& error){
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{SceneFormat};
        if(!checked(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)),"Query HDR target support",error))return false;
        const auto required=D3D12_FORMAT_SUPPORT1_RENDER_TARGET|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
        if((support.Support1&required)!=required){error="The Direct3D 12 adapter does not support the HDR scene format";return false;}
        // A diagnostic/performance cap also lets native validation exercise
        // the supported 2x path and the 1x alias path on a 4x-capable adapter.
        UINT sampleLimit=4;wchar_t limitText[8]{};
        DWORD limitLength=GetEnvironmentVariableW(L"MERIDIAN_MSAA_LIMIT",limitText,8);
        if(limitLength==1&&(limitText[0]==L'1'||limitText[0]==L'2'||limitText[0]==L'4'))sampleLimit=UINT(limitText[0]-L'0');
        else if(limitLength)std::fputs("Ignoring invalid MERIDIAN_MSAA_LIMIT (expected 1, 2 or 4).\n",stderr);
        sceneSamples=1;
        if(support.Support1&D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE){
            for(UINT samples:{4u,2u}){
                if(samples>sampleLimit)continue;
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS color{SceneFormat,samples,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS depthSupport{DepthFormat,samples,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};
                if(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&color,sizeof(color)))&&color.NumQualityLevels&&
                   SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&depthSupport,sizeof(depthSupport)))&&depthSupport.NumQualityLevels){sceneSamples=samples;break;}
            }
        }
        std::fprintf(stderr,"Scene target: R16G16B16A16_FLOAT; samples=%u; requested limit=%u\n",sceneSamples,sampleLimit);return true;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV()const{
        auto handle=rtvHeap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=FrameCount*rtvStride;return handle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE sceneSRV()const{
        auto handle=resourceHeap->GetGPUDescriptorHandleForHeapStart();handle.ptr+=srvStride;return handle;
    }
    bool createTargets(std::string& error){
        auto handle=rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<FrameCount;++i){if(!checked(swapChain->GetBuffer(i,IID_PPV_ARGS(&backBuffers[i])),"Get swap-chain buffer",error))return false;device->CreateRenderTargetView(backBuffers[i].Get(),nullptr,handle);handle.ptr+=rtvStride;}
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DepthFormat;desc.SampleDesc.Count=sceneSamples;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=DepthFormat;clear.DepthStencil.Depth=0;
        auto properties=heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        if(!checked(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"Create multisample scene depth",error))return false;
        device->CreateDepthStencilView(depth.Get(),nullptr,dsvHeap->GetCPUDescriptorHandleForHeapStart());
        desc.Format=SceneFormat;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;clear={};clear.Format=SceneFormat;clear.Color[3]=1;
        if(!checked(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&sceneColor)),"Create HDR scene target",error))return false;
        device->CreateRenderTargetView(sceneColor.Get(),nullptr,sceneRTV());
        if(sceneSamples>1){
            desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
            if(!checked(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&resolvedColor)),"Create resolved HDR scene",error))return false;
        }else resolvedColor=sceneColor;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=SceneFormat;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
        auto descriptor=resourceHeap->GetCPUDescriptorHandleForHeapStart();descriptor.ptr+=srvStride;
        device->CreateShaderResourceView(resolvedColor.Get(),&view,descriptor);return true;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDSV()const{
        auto handle=dsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);return handle;
    }
    bool createShadowTarget(std::string& error){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=ShadowSize;desc.Height=ShadowSize;desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=DepthFormat;clear.DepthStencil.Depth=1;
        auto heap=heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        if(!checked(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&shadowDepth)),"Create directional shadow map",error))return false;
        D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};depthView.Format=DepthFormat;depthView.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(shadowDepth.Get(),&depthView,shadowDSV());
        D3D12_SHADER_RESOURCE_VIEW_DESC readView{};readView.Format=DXGI_FORMAT_R32_FLOAT;readView.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;readView.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;readView.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(shadowDepth.Get(),&readView,resourceHeap->GetCPUDescriptorHandleForHeapStart());return true;
    }
    bool createPipelines(std::string& error){
        D3D12_ROOT_PARAMETER parameters[8]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[0].Descriptor.ShaderRegister=0;parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        for(UINT i=1;i<4;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[i].Descriptor.ShaderRegister=i-1;parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
        D3D12_DESCRIPTOR_RANGE shadowRange{};shadowRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;shadowRange.NumDescriptors=1;shadowRange.BaseShaderRegister=3;
        parameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[4].DescriptorTable.NumDescriptorRanges=1;parameters[4].DescriptorTable.pDescriptorRanges=&shadowRange;parameters[4].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[5].Descriptor.ShaderRegister=4;parameters[5].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC shadowSampler{};shadowSampler.Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;shadowSampler.AddressU=shadowSampler.AddressV=shadowSampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        shadowSampler.ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;shadowSampler.BorderColor=D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;shadowSampler.MaxAnisotropy=1;shadowSampler.MaxLOD=D3D12_FLOAT32_MAX;shadowSampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE sceneRange{};sceneRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;sceneRange.NumDescriptors=1;sceneRange.BaseShaderRegister=5;
        parameters[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[6].DescriptorTable.NumDescriptorRanges=1;parameters[6].DescriptorTable.pDescriptorRanges=&sceneRange;parameters[6].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[7].Descriptor.ShaderRegister=6;parameters[7].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC samplers[2]={shadowSampler,shadowSampler};samplers[1].Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;samplers[1].AddressU=samplers[1].AddressV=samplers[1].AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;samplers[1].ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;samplers[1].ShaderRegister=1;
        D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=8;root.pParameters=parameters;root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;root.NumStaticSamplers=2;root.pStaticSamplers=samplers;
        ComPtr<ID3DBlob> serialized,diagnostics;
        HRESULT result=D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&diagnostics);
        if(!checked(result,"Serialize root signature",error)){if(diagnostics)error.append(static_cast<const char*>(diagnostics->GetBufferPointer()),diagnostics->GetBufferSize());return false;}
        if(!checked(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&rootSignature)),"Create root signature",error))return false;
        D3D12_INPUT_ELEMENT_DESC layout[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32_FLOAT,0,36,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=rootSignature.Get();p.VS={g_world_vs,sizeof(g_world_vs)};p.PS={g_world_ps,sizeof(g_world_ps)};
        p.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        p.BlendState.RenderTarget[0].SrcBlend=D3D12_BLEND_ONE;p.BlendState.RenderTarget[0].DestBlend=D3D12_BLEND_ZERO;p.BlendState.RenderTarget[0].BlendOp=D3D12_BLEND_OP_ADD;
        p.BlendState.RenderTarget[0].SrcBlendAlpha=D3D12_BLEND_ONE;p.BlendState.RenderTarget[0].DestBlendAlpha=D3D12_BLEND_ZERO;p.BlendState.RenderTarget[0].BlendOpAlpha=D3D12_BLEND_OP_ADD;p.BlendState.RenderTarget[0].LogicOp=D3D12_LOGIC_OP_NOOP;
        p.SampleMask=UINT_MAX;p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
        p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        p.DepthStencilState.StencilReadMask=D3D12_DEFAULT_STENCIL_READ_MASK;p.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_WRITE_MASK;
        p.DepthStencilState.FrontFace.StencilFailOp=D3D12_STENCIL_OP_KEEP;p.DepthStencilState.FrontFace.StencilDepthFailOp=D3D12_STENCIL_OP_KEEP;p.DepthStencilState.FrontFace.StencilPassOp=D3D12_STENCIL_OP_KEEP;p.DepthStencilState.FrontFace.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;p.DepthStencilState.BackFace=p.DepthStencilState.FrontFace;
        p.InputLayout={layout,4};p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=SceneFormat;p.DSVFormat=DepthFormat;p.SampleDesc.Count=sceneSamples;
        if(!checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&worldPipeline)),"Create world pipeline (Shader Model 6 support required)",error))return false;
        p.PS={g_world_far_ps,sizeof(g_world_far_ps)};
        if(!checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&farPipeline)),"Create distant world pipeline",error))return false;
        if(raySupported){p.PS={g_world_rt_ps,sizeof(g_world_rt_ps)};if(FAILED(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&rayPipeline))))raySupported=false;}
        auto shadow=p;shadow.VS={g_shadow_vs,sizeof(g_shadow_vs)};shadow.PS={nullptr,0};shadow.NumRenderTargets=0;shadow.RTVFormats[0]=DXGI_FORMAT_UNKNOWN;shadow.SampleDesc.Count=1;shadow.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        shadow.RasterizerState.DepthBias=250;shadow.RasterizerState.SlopeScaledDepthBias=1.0f;
        // Depth clamp retains off-screen tall casters at the light frustum planes.
        shadow.RasterizerState.DepthClipEnable=FALSE;
        if(!checked(device->CreateGraphicsPipelineState(&shadow,IID_PPV_ARGS(&shadowPipeline)),"Create directional shadow pipeline",error))return false;
        p.VS={g_sky_vs,sizeof(g_sky_vs)};p.PS={g_sky_ps,sizeof(g_sky_ps)};p.InputLayout={nullptr,0};p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        if(!checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&skyPipeline)),"Create sky pipeline",error))return false;
        p.PS={g_post_ps,sizeof(g_post_ps)};p.SampleDesc.Count=1;p.RTVFormats[0]=ColorFormat;p.DepthStencilState.DepthEnable=FALSE;p.DSVFormat=DXGI_FORMAT_UNKNOWN;
        if(!checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&postPipeline)),"Create HDR tone-map pipeline",error))return false;
        D3D12_INPUT_ELEMENT_DESC uiLayout[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        p.VS={g_ui_vs,sizeof(g_ui_vs)};p.PS={g_ui_ps,sizeof(g_ui_ps)};p.InputLayout={uiLayout,2};p.DepthStencilState.DepthEnable=FALSE;
        p.BlendState.RenderTarget[0].BlendEnable=TRUE;p.BlendState.RenderTarget[0].SrcBlend=D3D12_BLEND_SRC_ALPHA;p.BlendState.RenderTarget[0].DestBlend=D3D12_BLEND_INV_SRC_ALPHA;p.BlendState.RenderTarget[0].DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
        return checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&uiPipeline)),"Create interface pipeline",error);
    }
    bool collectRetired(std::string& error){
        uint64_t completed=fence->GetCompletedValue();
        if(completed==UINT64_MAX){checked(device->GetDeviceRemovedReason(),"Device removed during stream retirement",error);return false;}
        while(!uploadBatches.empty()&&uploadBatches.front().fence<=completed)uploadBatches.pop_front();
        while(!retiredChunks.empty()&&retiredChunks.front().fence<=completed){
            const auto& chunk=*retiredChunks.front().chunk;
            vertexAllocator.release(chunk.vertices);indexAllocator.release(chunk.indices);retiredChunks.pop_front();
        }
        while(!retiredScenes.empty()&&retiredScenes.front().fence<=completed)retiredScenes.pop_front();
        return true;
    }
    StreamStats stats()const{
        StreamStats result=streaming;result.residentChunks=uint32_t(resident.size());
        result.pendingBatches=uint32_t(uploadBatches.size());
        for(const auto& tile:activeChunks){
            const auto& chunk=*tile.chunk;const size_t lod=size_t(chunk.key.lod);
            result.residentBytes+=chunk.geometryBytes();++result.residentTilesByLod[lod];result.residentBytesByLod[lod]+=chunk.geometryBytes();
        }
        result.rayInstances=raySupported?uint32_t(rayResidents.size()):0;
        for(const auto& batch:uploadBatches)result.pendingUploadBytes+=batch.stagingBytes;
        for(const auto& item:retiredChunks)result.retiredBytes+=item.chunk->geometryBytes()+item.chunk->blasBytes;
        for(const auto& item:retiredScenes){if(item.tlas)result.retiredBytes+=item.tlas->GetDesc().Width;if(item.metadata)result.retiredBytes+=item.metadata->GetDesc().Width;}
        return result;
    }
    bool stagingRoom(uint64_t bytes,std::string& error){
        if(bytes>MaxStagingBytes){error="World stream staging exceeds the 384 MiB hard limit";return false;}
        while(!uploadBatches.empty()&&(uploadBatches.size()>=MaxUploadBatches||stats().pendingUploadBytes+bytes>MaxStagingBytes)){
            ++streaming.pressureWaits;
            if(!wait(uploadBatches.front().fence,error)||!collectRetired(error))return false;
        }
        return true;
    }
    bool createArenas(uint64_t requiredVertices,uint64_t requiredIndices,bool distant,std::string& error){
        uint64_t vertexBytes=distant?128*MiB:DefaultVertexBudget;
        while(vertexBytes<streaming.vertexArenaBytes)vertexBytes*=2;
        uint64_t indexBytes=std::max(distant?32*MiB:DefaultIndexBudget,streaming.indexArenaBytes);
        // Two visible sets provide room for a teleport as well as ordinary
        // seven-chunk boundaries. Hard caps are never exceeded or truncated.
        const uint64_t vertexTarget=std::min(MaxVertexBudget,requiredVertices*2);
        const uint64_t indexTarget=std::min(MaxIndexBudget,requiredIndices*2);
        while(vertexBytes<vertexTarget)vertexBytes=std::min(MaxVertexBudget,vertexBytes*2);
        while(indexBytes<indexTarget)indexBytes=std::min(MaxIndexBudget,indexBytes*2);
        vertexBytes=(vertexBytes/sizeof(Vertex))*sizeof(Vertex);
        // Allocate replacements before publishing. An allocation failure keeps
        // the old coherent scene available; the temporary overlap exists only
        // during this rare, explicitly synchronized arena replacement.
        ComPtr<ID3D12Resource> vertices,indices;
        if(!createBuffer(vertexBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_GENERIC_READ,vertices,error)||
           !createBuffer(indexBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_GENERIC_READ,indices,error))return false;
        streaming.retiredChunks+=resident.size();resident.clear();activeChunks.clear();rayResidents.clear();
        retiredChunks.clear();retiredScenes.clear();tlas.Reset();instanceMetadata.Reset();
        worldVertices=std::move(vertices);worldIndices=std::move(indices);
        streaming.vertexArenaBytes=vertexBytes;streaming.indexArenaBytes=indexBytes;
        vertexAllocator.reset(uint32_t(vertexBytes/sizeof(Vertex)));indexAllocator.reset(uint32_t(indexBytes/sizeof(uint32_t)));
        worldVB={worldVertices->GetGPUVirtualAddress(),UINT(vertexBytes),sizeof(Vertex)};
        worldIB={worldIndices->GetGPUVirtualAddress(),UINT(indexBytes),DXGI_FORMAT_R32_UINT};
        worldPublished=false;
        std::fprintf(stderr,"World stream arenas: vertex=%llu index=%llu bytes; limits=256/64 MiB, staging=384 MiB/4 batches.\n",static_cast<unsigned long long>(vertexBytes),static_cast<unsigned long long>(indexBytes));
        return true;
    }
    D3D12_RAYTRACING_GEOMETRY_DESC rayGeometry(const ResidentChunk& chunk)const{
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexBuffer.StartAddress=worldVB.BufferLocation+uint64_t(chunk.vertices.first)*sizeof(Vertex);
        geometry.Triangles.VertexBuffer.StrideInBytes=sizeof(Vertex);geometry.Triangles.VertexCount=chunk.vertices.count;geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.IndexBuffer=worldIB.BufferLocation+uint64_t(chunk.indices.first)*sizeof(uint32_t);
        geometry.Triangles.IndexCount=chunk.indices.count;geometry.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT;return geometry;
    }
    bool prepareRayScene(UploadBatch& batch,const std::vector<std::shared_ptr<ResidentChunk>>& ordered,
                         const std::vector<std::shared_ptr<ResidentChunk>>& added,std::string& error){
        if(!checked(batch.commands.As(&batch.rayCommands),"Get stream ray-tracing command interface",error))return false;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};bottom.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottom.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;bottom.NumDescs=1;bottom.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        uint64_t scratchBytes=0;
        for(const auto& chunk:added){
            if(!chunk->indices.count)continue;
            auto geometry=rayGeometry(*chunk);bottom.pGeometryDescs=&geometry;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};device5->GetRaytracingAccelerationStructurePrebuildInfo(&bottom,&size);
            if(!size.ResultDataMaxSizeInBytes||!size.ScratchDataSizeInBytes){error="Chunk BLAS sizing failed";return false;}
            if(!createBuffer(size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,chunk->blas,error,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return false;
            chunk->blasBytes=size.ResultDataMaxSizeInBytes;scratchBytes=std::max(scratchBytes,size.ScratchDataSizeInBytes);
        }
        struct InstanceMetadata {uint32_t firstVertex,firstIndex;};
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
        std::vector<InstanceMetadata> metadata;
        for(const auto& chunk:ordered){
            if(!chunk->indices.count)continue;
            if(!chunk->blas){error="A resident chunk is missing its ray-tracing geometry";return false;}
            D3D12_RAYTRACING_INSTANCE_DESC instance{};instance.Transform[0][0]=instance.Transform[1][1]=instance.Transform[2][2]=1;
            instance.InstanceID=UINT(metadata.size());instance.InstanceMask=255;instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            instance.AccelerationStructure=chunk->blas->GetGPUVirtualAddress();instances.push_back(instance);metadata.push_back({chunk->vertices.first,chunk->indices.first});
        }
        if(instances.empty())return true;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};top.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        top.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;top.NumDescs=UINT(instances.size());top.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topSize{};device5->GetRaytracingAccelerationStructurePrebuildInfo(&top,&topSize);
        if(!topSize.ResultDataMaxSizeInBytes||!topSize.ScratchDataSizeInBytes){error="World TLAS sizing failed";return false;}
        scratchBytes=std::max(scratchBytes,topSize.ScratchDataSizeInBytes);
        if(!createBuffer(topSize.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,batch.tlas,error,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
           !createBuffer(scratchBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,batch.scratch,error,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
           !createBuffer(instances.size()*sizeof(instances[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,batch.instances,error)||
           !createBuffer(metadata.size()*sizeof(metadata[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,batch.metadata,error)||
           !writeUpload(batch.instances.Get(),instances.data(),instances.size()*sizeof(instances[0]),error)||
           !writeUpload(batch.metadata.Get(),metadata.data(),metadata.size()*sizeof(metadata[0]),error))return false;
        // No fallible operations after the first AS build is recorded: a
        // preparation failure can safely continue through the raster path.
        for(const auto& chunk:added){
            if(!chunk->indices.count)continue;
            auto geometry=rayGeometry(*chunk);bottom.pGeometryDescs=&geometry;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=bottom;build.DestAccelerationStructureData=chunk->blas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=batch.scratch->GetGPUVirtualAddress();
            batch.rayCommands->BuildRaytracingAccelerationStructure(&build,0,nullptr);uavBarrier(batch.commands.Get(),chunk->blas.Get());uavBarrier(batch.commands.Get(),batch.scratch.Get());
        }
        top.InstanceDescs=batch.instances->GetGPUVirtualAddress();
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=top;build.DestAccelerationStructureData=batch.tlas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=batch.scratch->GetGPUVirtualAddress();
        batch.rayCommands->BuildRaytracingAccelerationStructure(&build,0,nullptr);uavBarrier(batch.commands.Get(),batch.tlas.Get());return true;
    }
    enum class DrawPass {Shadow,Main,Far};
    void drawWorld(const ClipVolume& volume,DrawPass pass){
        if(activeChunks.empty())return;
        commands->IASetVertexBuffers(0,1,&worldVB);commands->IASetIndexBuffer(&worldIB);
        for(const auto& tile:activeChunks){
            const auto& chunk=*tile.chunk;if(!chunk.indices.count)continue;
            if(pass==DrawPass::Shadow){if(!tile.shadowCaster)continue;}
            else if((chunk.key.lod==WorldLod::Far)!=(pass==DrawPass::Far))continue;
            if(!volume.visible(chunk.bounds)){
                if(pass==DrawPass::Shadow)++streaming.shadowCulled;else ++streaming.mainCulled;continue;
            }
            commands->DrawIndexedInstanced(chunk.indices.count,1,chunk.indices.first,INT(chunk.vertices.first),0);
            if(pass==DrawPass::Shadow)++streaming.shadowDrawn;else ++streaming.mainDrawn;
        }
    }

};
Renderer::Renderer():impl(new Impl){}
Renderer::~Renderer()=default;
bool Renderer::initialize(void* window,uint32_t width,uint32_t height,std::string& error,bool warp){
    auto& p=*impl;p.width=std::max(width,1u);p.height=std::max(height,1u);
    if(!window){error="Renderer requires a Win32 window";return false;}
#if defined(MC_DEBUG) && MC_DEBUG
    // Graphics Tools is optional. Its absence must never prevent a player launch.
    // Enabling the layer after device creation would remove that device.
    ComPtr<ID3D12Debug> debugLayer;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)))){
        debugLayer->EnableDebugLayer();
        std::fputs("D3D12 debug layer enabled.\n",stderr);
    }else{
        std::fputs("D3D12 debug layer unavailable; graphics validation is unverified.\n",stderr);
    }
#endif
    if(!checked(CreateDXGIFactory2(0,IID_PPV_ARGS(&p.factory)),"Create DXGI factory",error))return false;
    ComPtr<IDXGIAdapter1> chosen;
    if(warp){if(!checked(p.factory->EnumWarpAdapter(IID_PPV_ARGS(&chosen)),"Select WARP software adapter",error))return false;}
    else {
        ComPtr<IDXGIFactory6> factory6;p.factory.As(&factory6);
        for(UINT i=0;;++i){
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT hr=factory6?factory6->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)):p.factory->EnumAdapters1(i,&adapter);
            if(hr==DXGI_ERROR_NOT_FOUND)break;if(FAILED(hr))continue;
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),nullptr))){chosen=adapter;break;}
        }
        if(!chosen){error="No Direct3D 12 hardware adapter was found";return false;}
    }
    if(!checked(D3D12CreateDevice(chosen.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&p.device)),"Create Direct3D 12 device",error))return false;
#if defined(MC_DEBUG) && MC_DEBUG
    if(debugLayer&&SUCCEEDED(p.device.As(&p.debugMessages))){
        p.debugMessages->SetMessageCountLimit(4096);
        D3D12_MESSAGE_SEVERITY ignored[]={D3D12_MESSAGE_SEVERITY_WARNING,D3D12_MESSAGE_SEVERITY_INFO,D3D12_MESSAGE_SEVERITY_MESSAGE};
        D3D12_INFO_QUEUE_FILTER filter{};filter.DenyList.NumSeverities=3;filter.DenyList.pSeverityList=ignored;
        p.debugMessages->AddStorageFilterEntries(&filter);
        std::fputs("D3D12 corruption/error message validation enabled.\n",stderr);
    }else{
        std::fputs("D3D12 InfoQueue unavailable; graphics validation is unverified.\n",stderr);
    }
#endif
    DXGI_ADAPTER_DESC1 adapterDescription{};chosen->GetDesc1(&adapterDescription);char name[256]{};WideCharToMultiByte(CP_UTF8,0,adapterDescription.Description,-1,name,sizeof(name),nullptr,nullptr);p.gpuName=name;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_5};
    p.raySupported=SUCCEEDED(p.device.As(&p.device5))&&SUCCEEDED(p.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options)))&&options.RaytracingTier>=D3D12_RAYTRACING_TIER_1_1&&SUCCEEDED(p.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shaderModel,sizeof(shaderModel)))&&shaderModel.HighestShaderModel>=D3D_SHADER_MODEL_6_5;
    ComPtr<IDXGIFactory5> factory5;if(SUCCEEDED(p.factory.As(&factory5))){BOOL allowed=FALSE;p.tearing=SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&allowed,sizeof(allowed)))&&allowed;}
    D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(!checked(p.device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&p.queue)),"Create graphics command queue",error))return false;
    DXGI_SWAP_CHAIN_DESC1 swap{};swap.Width=p.width;swap.Height=p.height;swap.Format=ColorFormat;swap.SampleDesc.Count=1;swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swap.BufferCount=FrameCount;swap.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;swap.Flags=p.tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0;
    ComPtr<IDXGISwapChain1> chain;
    if(!checked(p.factory->CreateSwapChainForHwnd(p.queue.Get(),static_cast<HWND>(window),&swap,nullptr,nullptr,&chain),"Create swap chain",error)||!checked(chain.As(&p.swapChain),"Get swap-chain interface",error))return false;
    p.factory->MakeWindowAssociation(static_cast<HWND>(window),DXGI_MWA_NO_ALT_ENTER);
    if(!checked(p.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&p.fence)),"Create GPU fence",error))return false;
    p.fenceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!p.fenceEvent)return checked(HRESULT_FROM_WIN32(GetLastError()),"Create fence event",error);
    for(auto& frame:p.frames){
        if(!checked(p.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&frame.allocator)),"Create frame allocator",error)||!p.createBuffer(256,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,frame.constants,error)||
           !p.createBuffer(MaxLights*sizeof(Light),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,frame.lights,error))return false;
        void* mapped=nullptr;D3D12_RANGE read{0,0};if(!checked(frame.constants->Map(0,&read,&mapped),"Map frame constants",error))return false;frame.mappedConstants=static_cast<uint8_t*>(mapped);
    }
    if(!checked(p.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,p.frames[0].allocator.Get(),nullptr,IID_PPV_ARGS(&p.commands)),"Create command list",error)||!checked(p.commands->Close(),"Initialize command list",error))return false;
    if(p.raySupported&&FAILED(p.commands.As(&p.commands4)))p.raySupported=false;
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;descriptors.NumDescriptors=FrameCount+1;
    if(!checked(p.device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&p.rtvHeap)),"Create render-target descriptors",error))return false;
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;descriptors.NumDescriptors=2;if(!checked(p.device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&p.dsvHeap)),"Create depth descriptor",error))return false;
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;descriptors.NumDescriptors=2;descriptors.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(!checked(p.device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&p.resourceHeap)),"Create shader-resource descriptors",error))return false;
    p.rtvStride=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    p.srvStride=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    std::fprintf(stderr,"Scene depth: reverse-Z; near=%.2fm; far=%.0fm; shadows=forward-Z\n",SceneNear,SceneFar);
    p.initializeTiming();
    return p.selectSceneSamples(error)&&p.createTargets(error)&&p.createShadowTarget(error)&&p.createPipelines(error)&&p.checkDebugMessages(error);
}
bool Renderer::setWorld(const World& world,uint64_t epoch,std::string& error){
    auto& p=*impl;if(!p.device){error="Renderer is not initialized";return false;}
    struct WorldTimer {
        Impl& owner;uint64_t start;
        ~WorldTimer(){const double ms=owner.cpuElapsed(start,owner.cpuTick());auto& t=owner.timing;++t.worldCalls;t.worldLastMs=ms;t.worldTotalMs+=ms;t.worldMaxMs=std::max(t.worldMaxMs,ms);}
    } worldTimer{p,p.cpuTick()};
    if(!p.collectRetired(error))return false;
    if(p.worldPublished&&p.streaming.epoch==epoch&&p.worldRevision==world.renderRevision)return true;
    const auto views=world.renderTiles();
    uint64_t requiredVertices=0,requiredIndices=0;
    std::map<std::pair<int,int>,bool> selectedCells;
    if(views.size()>0xffffff){error="World stream exceeds the DXR instance identifier limit";return false;}
    for(const auto& view:views){
        if(!view.mesh||size_t(view.key.lod)>size_t(WorldLod::Far)){error="World render view contains an invalid mesh or LOD";return false;}
        if(!selectedCells.emplace(std::pair{view.key.x,view.key.z},true).second){error="World render view contains overlapping LODs for one cell";return false;}
        const auto& mesh=*view.mesh;
        if(mesh.vertices.size()>MaxVertexBudget/sizeof(Vertex)||mesh.indices.size()>MaxIndexBudget/sizeof(uint32_t)){
            error="A world tile exceeds the 256 MiB vertex / 64 MiB index arena limits";return false;
        }
        requiredVertices+=mesh.vertices.size()*sizeof(Vertex);requiredIndices+=mesh.indices.size()*sizeof(uint32_t);
        if(mesh.indices.size()%3){error="World tile has an incomplete triangle";return false;}
    }
    if(requiredVertices>MaxVertexBudget||requiredIndices>MaxIndexBudget){error="Visible world exceeds the 256 MiB vertex / 64 MiB index hard budget; no tiles were dropped";return false;}
    // Repack waits before taking this snapshot. Keep the prior coherent scene
    // available until replacement submission succeeds (bounded 2x arena peak).
    struct ArenaBackup {
        Impl& p;bool committed=false;
        ComPtr<ID3D12Resource> vertices,indices,tlas,metadata;
        D3D12_VERTEX_BUFFER_VIEW vb;D3D12_INDEX_BUFFER_VIEW ib;
        ArenaAllocator vertexAllocator,indexAllocator;
        Impl::ChunkMap resident;std::vector<Impl::ActiveTile> active;
        std::vector<std::shared_ptr<Impl::ResidentChunk>> rays;
        StreamStats stats;bool published;
        explicit ArenaBackup(Impl& owner):p(owner),vertices(p.worldVertices),indices(p.worldIndices),tlas(p.tlas),metadata(p.instanceMetadata),vb(p.worldVB),ib(p.worldIB),
            vertexAllocator(p.vertexAllocator),indexAllocator(p.indexAllocator),resident(p.resident),active(p.activeChunks),rays(p.rayResidents),stats(p.streaming),published(p.worldPublished){}
        ~ArenaBackup(){if(committed)return;
            p.worldVertices=std::move(vertices);p.worldIndices=std::move(indices);p.tlas=std::move(tlas);p.instanceMetadata=std::move(metadata);p.worldVB=vb;p.worldIB=ib;
            p.vertexAllocator=std::move(vertexAllocator);p.indexAllocator=std::move(indexAllocator);p.resident=std::move(resident);p.activeChunks=std::move(active);p.rayResidents=std::move(rays);
            stats.pressureWaits=p.streaming.pressureWaits;stats.repackWaits=p.streaming.repackWaits;p.streaming=stats;p.worldPublished=published;
        }
    };
    std::unique_ptr<ArenaBackup> backup;
    if(!p.worldVertices){backup=std::make_unique<ArenaBackup>(p);if(!p.createArenas(requiredVertices,requiredIndices,world.distantEnabled(),error))return false;}
    ArenaAllocator plannedVertices,plannedIndices;
    Impl::ChunkMap nextResident;
    std::vector<Impl::ActiveTile> ordered;
    std::vector<std::shared_ptr<Impl::ResidentChunk>> added,nextRays;
    std::vector<const Mesh*> addedSources;
    uint64_t retained=0,uploadVertexBytes=0,uploadIndexBytes=0;
    auto plan=[&](){
        plannedVertices=p.vertexAllocator;plannedIndices=p.indexAllocator;nextResident.clear();ordered.clear();added.clear();addedSources.clear();nextRays.clear();
        retained=uploadVertexBytes=uploadIndexBytes=0;
        for(const auto& view:views){
            const auto& source=*view.mesh;auto old=p.resident.find(view.key);
            std::shared_ptr<Impl::ResidentChunk> chunk;
            if(p.worldPublished&&p.streaming.epoch==epoch&&old!=p.resident.end()){
                chunk=old->second;++retained;
            }else{
                chunk=std::make_shared<Impl::ResidentChunk>();chunk->key=view.key;
                if(!plannedVertices.allocate(uint32_t(source.vertices.size()),chunk->vertices)||
                   !plannedIndices.allocate(uint32_t(source.indices.size()),chunk->indices))return false;
                added.push_back(chunk);addedSources.push_back(&source);
                uploadVertexBytes+=source.vertices.size()*sizeof(Vertex);uploadIndexBytes+=source.indices.size()*sizeof(uint32_t);
            }
            nextResident.emplace(view.key,chunk);ordered.push_back({chunk,view.rayTrace,view.shadowCaster});
            if(p.raySupported&&view.rayTrace&&chunk->indices.count)nextRays.push_back(chunk);
        }
        // Stable instance order ignores raster-only arrivals and source order.
        std::sort(nextRays.begin(),nextRays.end(),[](const auto& a,const auto& b){return a->key<b->key;});
        return true;
    };
    if(!plan()){
        while(!p.retiredChunks.empty()){
            ++p.streaming.pressureWaits;
            if(!p.wait(p.retiredChunks.front().fence,error)||!p.collectRetired(error))return false;
            if(plan())break;
        }
        if(!plan()){
            ++p.streaming.repackWaits;
            std::fputs("World stream repack waiting for GPU; replacing fragmented or undersized arenas.\n",stderr);
            if(!p.flush(error)||!p.collectRetired(error))return false;
            backup=std::make_unique<ArenaBackup>(p);
            if(!p.createArenas(requiredVertices,requiredIndices,world.distantEnabled(),error)||!plan()){
                if(error.empty())error="World stream arena repack could not fit the complete visible set";return false;
            }
        }
    }
    bool rayChanged=p.raySupported&&nextRays!=p.rayResidents;
    if(added.empty()&&nextResident.size()==p.resident.size()&&!rayChanged){
        // Prefetch cache revisions need not submit GPU work or inflate counters.
        p.activeChunks=std::move(ordered);p.streaming.epoch=epoch;p.streaming.renderRevision=world.renderRevision;
        p.worldRevision=world.renderRevision;p.worldPublished=true;
        if(backup)backup->committed=true;return true;
    }
    const uint64_t instanceCount=nextRays.size();
    const uint64_t instanceBytes=rayChanged&&instanceCount?std::max<uint64_t>(256,instanceCount*sizeof(D3D12_RAYTRACING_INSTANCE_DESC)):0;
    const uint64_t metadataBytes=rayChanged&&instanceCount?std::max<uint64_t>(256,instanceCount*8):0;
    const uint64_t stagingBytes=(uploadVertexBytes?std::max<uint64_t>(256,uploadVertexBytes):0)+(uploadIndexBytes?std::max<uint64_t>(256,uploadIndexBytes):0)+instanceBytes+metadataBytes;
    if(!p.stagingRoom(stagingBytes,error))return false;
    // Retirement may have coalesced the free lists while enforcing staging caps.
    if(!plan()){error="World stream ranges could not be allocated after staging retirement";return false;}
    rayChanged=p.raySupported&&nextRays!=p.rayResidents;
    for(size_t i=0;i<added.size();++i){
        const auto& source=*addedSources[i];
        if(!meshBounds(source,added[i]->bounds)){error="World tile contains a non-finite vertex position";return false;}
        for(uint32_t index:source.indices)if(index>=source.vertices.size()){error="World tile triangle references a missing vertex";return false;}
    }
    std::vector<std::shared_ptr<Impl::ResidentChunk>> rayBuilds;
    if(rayChanged)for(const auto& chunk:nextRays)if(!chunk->blas)rayBuilds.push_back(chunk);
    Impl::UploadBatch batch;batch.stagingBytes=stagingBytes;batch.arenaVertices=p.worldVertices;batch.arenaIndices=p.worldIndices;
    batch.chunks=added;batch.chunks.insert(batch.chunks.end(),nextRays.begin(),nextRays.end());
    if(!checked(p.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&batch.allocator)),"Create stream command allocator",error)||
       !checked(p.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,batch.allocator.Get(),nullptr,IID_PPV_ARGS(&batch.commands)),"Create stream command list",error))return false;
    if(uploadVertexBytes&&!p.createBuffer(uploadVertexBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,batch.vertices,error))return false;
    if(uploadIndexBytes&&!p.createBuffer(uploadIndexBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,batch.indices,error))return false;
    auto fillStaging=[&](ID3D12Resource* resource,bool vertices){
        if(!resource)return true;void* data=nullptr;D3D12_RANGE noRead{0,0};
        if(!checked(resource->Map(0,&noRead,&data),"Map tile staging buffer",error))return false;
        size_t offset=0;
        for(const auto* source:addedSources){
            size_t bytes=vertices?source->vertices.size()*sizeof(Vertex):source->indices.size()*sizeof(uint32_t);
            if(bytes)std::memcpy(static_cast<uint8_t*>(data)+offset,vertices?static_cast<const void*>(source->vertices.data()):static_cast<const void*>(source->indices.data()),bytes);
            offset+=bytes;
        }
        D3D12_RANGE written{0,offset};resource->Unmap(0,&written);return true;
    };
    if(!fillStaging(batch.vertices.Get(),true)||!fillStaging(batch.indices.Get(),false))return false;
    if(uploadVertexBytes)transition(batch.commands.Get(),p.worldVertices.Get(),D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_STATE_COPY_DEST);
    if(uploadIndexBytes)transition(batch.commands.Get(),p.worldIndices.Get(),D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_STATE_COPY_DEST);
    uint64_t vertexOffset=0,indexOffset=0;
    for(const auto& chunk:added){
        uint64_t vertices=uint64_t(chunk->vertices.count)*sizeof(Vertex),indices=uint64_t(chunk->indices.count)*sizeof(uint32_t);
        if(vertices)batch.commands->CopyBufferRegion(p.worldVertices.Get(),uint64_t(chunk->vertices.first)*sizeof(Vertex),batch.vertices.Get(),vertexOffset,vertices);
        if(indices)batch.commands->CopyBufferRegion(p.worldIndices.Get(),uint64_t(chunk->indices.first)*sizeof(uint32_t),batch.indices.Get(),indexOffset,indices);
        vertexOffset+=vertices;indexOffset+=indices;
    }
    if(uploadVertexBytes)transition(batch.commands.Get(),p.worldVertices.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ);
    if(uploadIndexBytes)transition(batch.commands.Get(),p.worldIndices.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ);
    if(rayChanged&&!p.prepareRayScene(batch,nextRays,rayBuilds,error)){
        if(FAILED(p.device->GetDeviceRemovedReason()))return false;
        std::fprintf(stderr,"DXR stream preparation unavailable; continuing with raster rendering: %s\n",error.c_str());
        p.raySupported=false;batch.tlas.Reset();batch.metadata.Reset();batch.instances.Reset();batch.scratch.Reset();
        for(auto& chunk:rayBuilds){chunk->blas.Reset();chunk->blasBytes=0;}nextRays.clear();error.clear();
    }
    if(!checked(batch.commands->Close(),"Close tile upload command list",error))return false;
    const uint64_t priorTail=p.nextFence-1;batch.fence=p.nextFence++;
    p.uploadBatches.push_back(std::move(batch));auto& submitted=p.uploadBatches.back();
    ID3D12CommandList* lists[]={submitted.commands.Get()};p.queue->ExecuteCommandLists(1,lists);
    if(!checked(p.queue->Signal(p.fence.Get(),submitted.fence),"Signal tile upload completion",error))return false;
    for(const auto& [key,chunk]:p.resident){auto replacement=nextResident.find(key);if(replacement==nextResident.end()||replacement->second!=chunk){
        p.retiredChunks.push_back({priorTail,chunk});++p.streaming.retiredChunks;
    }}
    if(rayChanged){
        if(p.tlas||p.instanceMetadata)p.retiredScenes.push_back({priorTail,p.tlas,p.instanceMetadata});
        p.tlas=submitted.tlas;p.instanceMetadata=submitted.metadata;p.rayResidents=std::move(nextRays);
        if(p.tlas)++p.streaming.tlasBuilds;
        if(p.raySupported)p.streaming.blasBuilds+=rayBuilds.size();
    }
    p.vertexAllocator=std::move(plannedVertices);p.indexAllocator=std::move(plannedIndices);p.resident=std::move(nextResident);p.activeChunks=std::move(ordered);
    p.streaming.uploadedChunks+=added.size();p.streaming.retainedChunks+=retained;
    for(const auto& chunk:added)++p.streaming.uploadedTilesByLod[size_t(chunk->key.lod)];
    p.streaming.epoch=epoch;p.streaming.renderRevision=world.renderRevision;p.worldRevision=world.renderRevision;p.worldPublished=true;
    if(backup)backup->committed=true;
    return p.checkDebugMessages(error);
}
StreamStats Renderer::streamStats()const{return impl->stats();}
RenderTimingStats Renderer::timingStats()const{return impl->timingStats();}

bool Renderer::render(const RenderFrame& input,std::string& error){
    auto& p=*impl;if(!p.swapChain){error="Renderer is not initialized";return false;}
    const uint64_t renderStart=p.cpuTick();
    const UINT current=p.swapChain->GetCurrentBackBufferIndex();auto& frame=p.frames[current];
    const uint64_t waitStart=p.cpuTick();if(!p.wait(frame.fence,error))return false;
    const uint64_t prepareStart=p.cpuTick();
    p.collectTimings();if(!p.collectRetired(error))return false;
    const Mesh* dynamic=input.dynamic;size_t dynamicVertexBytes=dynamic?dynamic->vertices.size()*sizeof(Vertex):0,dynamicIndexBytes=dynamic?dynamic->indices.size()*sizeof(uint32_t):0,uiBytes=input.ui?input.ui->size()*sizeof(UiVertex):0;
    if(dynamicVertexBytes>UINT_MAX||dynamicIndexBytes>UINT_MAX||uiBytes>UINT_MAX){error="Frame geometry exceeds Direct3D buffer view limits";return false;}
    if(!p.ensureUpload(frame.dynamicVertices,frame.dynamicVertexCapacity,dynamicVertexBytes,error)||!p.ensureUpload(frame.dynamicIndices,frame.dynamicIndexCapacity,dynamicIndexBytes,error)||!p.ensureUpload(frame.ui,frame.uiCapacity,uiBytes,error))return false;
    if(dynamicVertexBytes&&!p.writeUpload(frame.dynamicVertices.Get(),dynamic->vertices.data(),dynamicVertexBytes,error))return false;
    if(dynamicIndexBytes&&!p.writeUpload(frame.dynamicIndices.Get(),dynamic->indices.data(),dynamicIndexBytes,error))return false;
    if(uiBytes&&!p.writeUpload(frame.ui.Get(),input.ui->data(),uiBytes,error))return false;
    Constants c{};const float fov=68*Pi/180,aspect=float(p.width)/float(p.height);
    c.viewProjection=multiply(lookAt(input.eye,input.target),reversePerspective(fov,aspect,SceneNear,SceneFar));
    const auto fog=visibilityRange(input.coverageRadius);
    c.visibility[0]=fog.start;c.visibility[1]=fog.end;c.visibility[2]=fog.horizontal?1.0f:0.0f;c.visibility[3]=SceneFar;
    p.streaming.fogStart=fog.start;p.streaming.fogEnd=fog.end;
    p.streaming.mainDrawn=p.streaming.mainCulled=p.streaming.shadowDrawn=p.streaming.shadowCulled=0;
    c.eyeTime[0]=input.eye.x;c.eyeTime[1]=input.eye.y;c.eyeTime[2]=input.eye.z;c.eyeTime[3]=input.time;
    float sunAngle=(input.dayTime-6)*Pi/12;Vec3 sun=normalized({std::cos(sunAngle),std::sin(sunAngle),.27f});float daylight=clamp((sun.y+.10f)/.30f,0,1);daylight=daylight*daylight*(3-2*daylight);
    c.sunDay[0]=sun.x;c.sunDay[1]=sun.y;c.sunDay[2]=sun.z;c.sunDay[3]=daylight;
    std::array<Light,MaxLights> visibleLights{};UINT lightCount=0;
    if(input.lights)for(const Light& light:*input.lights){
        // The first fraction of twilight activated dozens of spots at <0.25
        // intensity while daylight was >99.8%, costing a full per-pixel loop.
        // Skip spots below 0.5 peak intensity only above 98% daylight. Point
        // strobes/muzzle flashes and every light in darker scenes are retained.
        float peak=light.intensity*std::max(light.color.x,std::max(light.color.y,light.color.z));
        if(daylight>.98f&&light.cone>-.999f&&peak<.5f)continue;
        visibleLights[lightCount++]=light;if(lightCount==MaxLights)break;
    }
    if(lightCount&&!p.writeUpload(frame.lights.Get(),visibleLights.data(),lightCount*sizeof(Light),error))return false;
    bool ray=p.raySupported&&p.tlas&&input.rayTracing;c.weather[0]=clamp(input.rain,0,1);c.weather[1]=clamp(input.exposure,.25f,3);c.weather[2]=ray?1.0f:0.0f;c.weather[3]=1-daylight;
    Vec3 forward=normalized(input.target-input.eye),right=normalized(cross({0,1,0},forward)),up=cross(forward,right);
    c.cameraRight[0]=right.x;c.cameraRight[1]=right.y;c.cameraRight[2]=right.z;c.cameraRight[3]=std::tan(fov*.5f)*aspect;
    c.cameraUp[0]=up.x;c.cameraUp[1]=up.y;c.cameraUp[2]=up.z;c.cameraUp[3]=std::tan(fov*.5f);
    c.cameraForward[0]=forward.x;c.cameraForward[1]=forward.y;c.cameraForward[2]=forward.z;c.viewport[0]=float(p.width);c.viewport[1]=float(p.height);
    // Fixed world-space coverage avoids camera-dependent shadow resolution.
    // Snap the projected world origin to whole shadow texels to avoid shimmer.
    Vec3 lightDirection=normalized({sun.x,std::max(sun.y,.06f),sun.z});
    Vec3 lightCenter=input.eye+Vec3{forward.x,0,forward.z}*45.0f;
    lightCenter.y=std::isfinite(input.groundHeight)?input.groundHeight:0.0f;
    Mat4 lightView=lookAt(lightCenter+lightDirection*450.0f,lightCenter);
    Mat4 orthographic{};orthographic.m[0]=2/ShadowSpan;orthographic.m[5]=2/ShadowSpan;orthographic.m[10]=1/900.0f;orthographic.m[15]=1;
    c.lightProjection=multiply(lightView,orthographic);
    const float halfResolution=float(ShadowSize)*.5f;
    c.lightProjection.m[12]+=(std::round(c.lightProjection.m[12]*halfResolution)-c.lightProjection.m[12]*halfResolution)/halfResolution;
    c.lightProjection.m[13]+=(std::round(c.lightProjection.m[13]*halfResolution)-c.lightProjection.m[13]*halfResolution)/halfResolution;
    c.viewport[2]=1.0f/float(ShadowSize);c.viewport[3]=float(lightCount);
    const ClipVolume mainVolume(c.viewProjection),shadowVolume(c.lightProjection,true);
    std::memcpy(frame.mappedConstants,&c,sizeof(c));
    const uint64_t recordStart=p.cpuTick();
    if(!checked(frame.allocator->Reset(),"Reset frame allocator",error)||!checked(p.commands->Reset(frame.allocator.Get(),nullptr),"Reset frame command list",error))return false;
    // The direct queue has already completed preceding upload/AS lists when
    // this query executes. Pair slots are reused only after their frame fence.
    if(p.timing.gpuAvailable)p.commands->EndQuery(p.timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,current*2);
    p.commands->SetGraphicsRootSignature(p.rootSignature.Get());p.commands->SetGraphicsRootConstantBufferView(0,frame.constants->GetGPUVirtualAddress());
    p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    transition(p.commands.Get(),p.shadowDepth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto shadowDSV=p.shadowDSV();p.commands->ClearDepthStencilView(shadowDSV,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);p.commands->OMSetRenderTargets(0,nullptr,FALSE,&shadowDSV);
    D3D12_VIEWPORT lightViewport{0,0,float(ShadowSize),float(ShadowSize),0,1};D3D12_RECT lightScissor{0,0,LONG(ShadowSize),LONG(ShadowSize)};
    p.commands->RSSetViewports(1,&lightViewport);p.commands->RSSetScissorRects(1,&lightScissor);p.commands->SetPipelineState(p.shadowPipeline.Get());
    p.drawWorld(shadowVolume,Impl::DrawPass::Shadow);
    if(dynamicVertexBytes&&dynamicIndexBytes){D3D12_VERTEX_BUFFER_VIEW vb{frame.dynamicVertices->GetGPUVirtualAddress(),UINT(dynamicVertexBytes),sizeof(Vertex)};D3D12_INDEX_BUFFER_VIEW ib{frame.dynamicIndices->GetGPUVirtualAddress(),UINT(dynamicIndexBytes),DXGI_FORMAT_R32_UINT};p.commands->IASetVertexBuffers(0,1,&vb);p.commands->IASetIndexBuffer(&ib);p.commands->DrawIndexedInstanced(UINT(dynamic->indices.size()),1,0,0,0);}
    transition(p.commands.Get(),p.shadowDepth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]={p.resourceHeap.Get()};p.commands->SetDescriptorHeaps(1,heaps);p.commands->SetGraphicsRootDescriptorTable(4,p.resourceHeap->GetGPUDescriptorHandleForHeapStart());
    auto rtv=p.sceneRTV();auto dsv=p.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    const float clear[]={0,0,0,1};p.commands->ClearRenderTargetView(rtv,clear,0,nullptr);p.commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
    p.commands->OMSetRenderTargets(1,&rtv,FALSE,&dsv);D3D12_VIEWPORT viewport{0,0,float(p.width),float(p.height),0,1};D3D12_RECT scissor{0,0,LONG(p.width),LONG(p.height)};p.commands->RSSetViewports(1,&viewport);p.commands->RSSetScissorRects(1,&scissor);
    p.commands->SetGraphicsRootShaderResourceView(5,frame.lights->GetGPUVirtualAddress());
    if(ray){p.commands->SetGraphicsRootShaderResourceView(1,p.tlas->GetGPUVirtualAddress());p.commands->SetGraphicsRootShaderResourceView(2,p.worldVertices->GetGPUVirtualAddress());p.commands->SetGraphicsRootShaderResourceView(3,p.worldIndices->GetGPUVirtualAddress());p.commands->SetGraphicsRootShaderResourceView(7,p.instanceMetadata->GetGPUVirtualAddress());}
    p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);p.commands->SetPipelineState(p.skyPipeline.Get());p.commands->DrawInstanced(3,1,0,0);
    p.commands->SetPipelineState(ray?p.rayPipeline.Get():p.worldPipeline.Get());
    p.drawWorld(mainVolume,Impl::DrawPass::Main);
    if(dynamicVertexBytes&&dynamicIndexBytes){D3D12_VERTEX_BUFFER_VIEW vb{frame.dynamicVertices->GetGPUVirtualAddress(),UINT(dynamicVertexBytes),sizeof(Vertex)};D3D12_INDEX_BUFFER_VIEW ib{frame.dynamicIndices->GetGPUVirtualAddress(),UINT(dynamicIndexBytes),DXGI_FORMAT_R32_UINT};p.commands->IASetVertexBuffers(0,1,&vb);p.commands->IASetIndexBuffer(&ib);p.commands->DrawIndexedInstanced(UINT(dynamic->indices.size()),1,0,0,0);}
    if(std::any_of(p.activeChunks.begin(),p.activeChunks.end(),[](const auto& tile){return tile.chunk->key.lod==WorldLod::Far&&tile.chunk->indices.count;})){
        p.commands->SetPipelineState(p.farPipeline.Get());p.drawWorld(mainVolume,Impl::DrawPass::Far);
    }
    // Resolve in linear space before tone mapping. The 1x path aliases the
    // scene texture and uses a read/write transition instead of a resolve.
    if(p.sceneSamples>1){
        transition(p.commands.Get(),p.sceneColor.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        transition(p.commands.Get(),p.resolvedColor.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RESOLVE_DEST);
        p.commands->ResolveSubresource(p.resolvedColor.Get(),0,p.sceneColor.Get(),0,SceneFormat);
        transition(p.commands.Get(),p.sceneColor.Get(),D3D12_RESOURCE_STATE_RESOLVE_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(p.commands.Get(),p.resolvedColor.Get(),D3D12_RESOURCE_STATE_RESOLVE_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }else transition(p.commands.Get(),p.sceneColor.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(p.commands.Get(),p.backBuffers[current].Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv=p.rtvHeap->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=current*p.rtvStride;
    p.commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);p.commands->SetGraphicsRootDescriptorTable(6,p.sceneSRV());
    p.commands->SetPipelineState(p.postPipeline.Get());p.commands->DrawInstanced(3,1,0,0);
    if(p.sceneSamples==1)transition(p.commands.Get(),p.sceneColor.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    if(uiBytes){p.commands->SetPipelineState(p.uiPipeline.Get());D3D12_VERTEX_BUFFER_VIEW vb{frame.ui->GetGPUVirtualAddress(),UINT(uiBytes),sizeof(UiVertex)};p.commands->IASetVertexBuffers(0,1,&vb);p.commands->DrawInstanced(UINT(input.ui->size()),1,0,0);}
    transition(p.commands.Get(),p.backBuffers[current].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
    if(p.timing.gpuAvailable){
        p.commands->EndQuery(p.timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,current*2+1);
        p.commands->ResolveQueryData(p.timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,current*2,2,p.timestampReadback.Get(),uint64_t(current)*2*sizeof(uint64_t));
    }
    if(!checked(p.commands->Close(),"Close frame command list",error))return false;ID3D12CommandList* lists[]={p.commands.Get()};p.queue->ExecuteCommandLists(1,lists);
    const uint64_t presentStart=p.cpuTick();
    HRESULT presented=p.swapChain->Present(input.vsync?1:0,(!input.vsync&&p.tearing)?DXGI_PRESENT_ALLOW_TEARING:0);
    const uint64_t signalStart=p.cpuTick();
    frame.fence=p.nextFence++;if(!checked(p.queue->Signal(p.fence.Get(),frame.fence),"Signal frame completion",error))return false;
    const uint64_t signalEnd=p.cpuTick();
    if(!checked(presented,"Present frame",error)){HRESULT removed=p.device->GetDeviceRemovedReason();if(FAILED(removed)){char reason[64]{};std::snprintf(reason,sizeof(reason)," Device removed: 0x%08lX",static_cast<unsigned long>(removed));error+=reason;}return false;}
    p.lastPresented=current;p.hasPresented=true;++p.totalFrames;if(!p.checkDebugMessages(error))return false;
    FrameTiming sample;sample.frameIndex=p.totalFrames;
    sample.cpuRenderMs=p.cpuElapsed(renderStart,p.cpuTick());
    sample.cpuFenceWaitMs=p.cpuElapsed(waitStart,prepareStart);sample.cpuPrepareMs=p.cpuElapsed(prepareStart,recordStart);
    sample.cpuRecordSubmitMs=p.cpuElapsed(recordStart,presentStart)+p.cpuElapsed(signalStart,signalEnd);
    sample.cpuPresentMs=p.cpuElapsed(presentStart,signalStart);
    ++p.timing.submittedFrames;
    frame.timing=sample;frame.timingPending=true;
    return true;
}
bool Renderer::resize(uint32_t width,uint32_t height,std::string& error){
    auto& p=*impl;if(!p.swapChain||!width||!height||(p.width==width&&p.height==height))return true;
    if(!p.flush(error))return false;for(auto& buffer:p.backBuffers)buffer.Reset();p.depth.Reset();p.resolvedColor.Reset();p.sceneColor.Reset();
    p.width=width;p.height=height;p.hasPresented=false;
    if(!checked(p.swapChain->ResizeBuffers(FrameCount,width,height,ColorFormat,p.tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0),"Resize swap chain",error))return false;return p.createTargets(error)&&p.checkDebugMessages(error);
}
bool Renderer::rayTracingAvailable()const{return impl->raySupported;}
const char* Renderer::adapterName()const{return impl->gpuName.c_str();}
uint64_t Renderer::frameCount()const{return impl->totalFrames;}
bool Renderer::capture(const std::string& path,std::string& error){
    auto& p=*impl;if(!p.hasPresented){error="No rendered frame is available for capture";return false;}
    if(!p.beginImmediate(error))return false;
    auto* source=p.backBuffers[p.lastPresented].Get();D3D12_RESOURCE_DESC desc=source->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowSize=0,totalSize=0;p.device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowSize,&totalSize);
    ComPtr<ID3D12Resource> readback;if(!p.createBuffer(totalSize,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,readback,error)){p.commands->Close();return false;}
    transition(p.commands.Get(),source,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    p.commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);transition(p.commands.Get(),source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
    if(!p.endImmediate(error))return false;
    void* data=nullptr;D3D12_RANGE range{0,SIZE_T(totalSize)};if(!checked(readback->Map(0,&range,&data),"Map screenshot",error))return false;
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+p.width*p.height*4;
    info.biSize=sizeof(info);info.biWidth=LONG(p.width);info.biHeight=-LONG(p.height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=p.width*p.height*4;
    std::ofstream output(std::filesystem::path(std::u8string(path.begin(),path.end())),std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(&file),sizeof(file));output.write(reinterpret_cast<const char*>(&info),sizeof(info));
    std::vector<uint8_t> row(size_t(p.width)*4);const uint8_t* pixels=static_cast<const uint8_t*>(data)+footprint.Offset;
    for(UINT y=0;y<p.height;++y){const uint8_t* sourceRow=pixels+size_t(y)*footprint.Footprint.RowPitch;for(UINT x=0;x<p.width;++x){row[x*4]=sourceRow[x*4+2];row[x*4+1]=sourceRow[x*4+1];row[x*4+2]=sourceRow[x*4];row[x*4+3]=255;}output.write(reinterpret_cast<const char*>(row.data()),std::streamsize(row.size()));}
    D3D12_RANGE written{0,0};readback->Unmap(0,&written);output.close();if(!output){error="Could not write screenshot: "+path;return false;}return p.checkDebugMessages(error);
}
}
