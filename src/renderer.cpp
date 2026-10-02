#include "renderer.h"
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
#include "world_vs.h"
#include "shadow_vs.h"
#include "world_ps.h"
#include "world_rt_ps.h"
#include "sky_vs.h"
#include "sky_ps.h"
#include "ui_vs.h"
#include "ui_ps.h"

namespace mc {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT FrameCount=2;
constexpr UINT ShadowSize=2048;
constexpr UINT MaxLights=64;
constexpr float ShadowSpan=240.0f;
constexpr DXGI_FORMAT ColorFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DepthFormat=DXGI_FORMAT_D32_FLOAT;
struct Constants {
    Mat4 viewProjection;
    float eyeTime[4],sunDay[4],weather[4],cameraRight[4],cameraUp[4],cameraForward[4],viewport[4];
    Mat4 lightProjection;
};
static_assert(sizeof(Constants)<=256,"Frame constants must fit one aligned allocation");
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
}
struct Renderer::Impl {
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> constants,dynamicVertices,dynamicIndices,ui,lights;
        uint8_t* mappedConstants=nullptr;
        uint64_t fence=0,dynamicVertexCapacity=0,dynamicIndexCapacity=0,uiCapacity=0;
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
    ComPtr<ID3D12DescriptorHeap> rtvHeap,dsvHeap,shadowHeap;
    std::array<ComPtr<ID3D12Resource>,FrameCount> backBuffers;ComPtr<ID3D12Resource> depth,shadowDepth;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> worldPipeline,rayPipeline,skyPipeline,uiPipeline,shadowPipeline;
    ComPtr<ID3D12Resource> worldVertices,worldIndices,blas,tlas;
    D3D12_VERTEX_BUFFER_VIEW worldVB{};D3D12_INDEX_BUFFER_VIEW worldIB{};UINT worldIndexCount=0;
    UINT width=0,height=0,rtvStride=0,lastPresented=0;bool tearing=false,raySupported=false,hasPresented=false;
    std::string gpuName="Unavailable";
    ~Impl(){std::string ignored;if(queue&&fence&&fenceEvent)flush(ignored);for(auto& f:frames)if(f.constants&&f.mappedConstants)f.constants->Unmap(0,nullptr);if(fenceEvent)CloseHandle(fenceEvent);}
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
    bool flush(std::string& error){const uint64_t value=nextFence++;return checked(queue->Signal(fence.Get(),value),"Signal GPU fence",error)&&wait(value,error);}
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
    bool createTargets(std::string& error){
        auto handle=rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<FrameCount;++i){if(!checked(swapChain->GetBuffer(i,IID_PPV_ARGS(&backBuffers[i])),"Get swap-chain buffer",error))return false;device->CreateRenderTargetView(backBuffers[i].Get(),nullptr,handle);handle.ptr+=rtvStride;}
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DepthFormat;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=DepthFormat;clear.DepthStencil.Depth=1;
        auto properties=heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        if(!checked(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"Create depth target",error))return false;
        device->CreateDepthStencilView(depth.Get(),nullptr,dsvHeap->GetCPUDescriptorHandleForHeapStart());return true;
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
        device->CreateShaderResourceView(shadowDepth.Get(),&readView,shadowHeap->GetCPUDescriptorHandleForHeapStart());return true;
    }
    bool createPipelines(std::string& error){
        D3D12_ROOT_PARAMETER parameters[6]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[0].Descriptor.ShaderRegister=0;parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        for(UINT i=1;i<4;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[i].Descriptor.ShaderRegister=i-1;parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
        D3D12_DESCRIPTOR_RANGE shadowRange{};shadowRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;shadowRange.NumDescriptors=1;shadowRange.BaseShaderRegister=3;
        parameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[4].DescriptorTable.NumDescriptorRanges=1;parameters[4].DescriptorTable.pDescriptorRanges=&shadowRange;parameters[4].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[5].Descriptor.ShaderRegister=4;parameters[5].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC shadowSampler{};shadowSampler.Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;shadowSampler.AddressU=shadowSampler.AddressV=shadowSampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        shadowSampler.ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;shadowSampler.BorderColor=D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;shadowSampler.MaxAnisotropy=1;shadowSampler.MaxLOD=D3D12_FLOAT32_MAX;shadowSampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=6;root.pParameters=parameters;root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;root.NumStaticSamplers=1;root.pStaticSamplers=&shadowSampler;
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
        p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        p.DepthStencilState.StencilReadMask=D3D12_DEFAULT_STENCIL_READ_MASK;p.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_WRITE_MASK;
        p.DepthStencilState.FrontFace.StencilFailOp=D3D12_STENCIL_OP_KEEP;p.DepthStencilState.FrontFace.StencilDepthFailOp=D3D12_STENCIL_OP_KEEP;p.DepthStencilState.FrontFace.StencilPassOp=D3D12_STENCIL_OP_KEEP;p.DepthStencilState.FrontFace.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;p.DepthStencilState.BackFace=p.DepthStencilState.FrontFace;
        p.InputLayout={layout,4};p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=ColorFormat;p.DSVFormat=DepthFormat;p.SampleDesc.Count=1;
        if(!checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&worldPipeline)),"Create world pipeline (Shader Model 6 support required)",error))return false;
        if(raySupported){p.PS={g_world_rt_ps,sizeof(g_world_rt_ps)};if(FAILED(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&rayPipeline))))raySupported=false;}
        auto shadow=p;shadow.VS={g_shadow_vs,sizeof(g_shadow_vs)};shadow.PS={nullptr,0};shadow.NumRenderTargets=0;shadow.RTVFormats[0]=DXGI_FORMAT_UNKNOWN;
        shadow.RasterizerState.DepthBias=250;shadow.RasterizerState.SlopeScaledDepthBias=1.0f;
        // Depth clamp retains off-screen tall casters at the light frustum planes.
        shadow.RasterizerState.DepthClipEnable=FALSE;
        if(!checked(device->CreateGraphicsPipelineState(&shadow,IID_PPV_ARGS(&shadowPipeline)),"Create directional shadow pipeline",error))return false;
        p.VS={g_sky_vs,sizeof(g_sky_vs)};p.PS={g_sky_ps,sizeof(g_sky_ps)};p.InputLayout={nullptr,0};p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        if(!checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&skyPipeline)),"Create sky pipeline",error))return false;
        D3D12_INPUT_ELEMENT_DESC uiLayout[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        p.VS={g_ui_vs,sizeof(g_ui_vs)};p.PS={g_ui_ps,sizeof(g_ui_ps)};p.InputLayout={uiLayout,2};p.DepthStencilState.DepthEnable=FALSE;
        p.BlendState.RenderTarget[0].BlendEnable=TRUE;p.BlendState.RenderTarget[0].SrcBlend=D3D12_BLEND_SRC_ALPHA;p.BlendState.RenderTarget[0].DestBlend=D3D12_BLEND_INV_SRC_ALPHA;p.BlendState.RenderTarget[0].DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
        return checked(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&uiPipeline)),"Create interface pipeline",error);
    }
    bool buildRayScene(std::string& error){
        blas.Reset();tlas.Reset();if(!raySupported||!worldIndexCount)return true;
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexBuffer.StartAddress=worldVB.BufferLocation;geometry.Triangles.VertexBuffer.StrideInBytes=sizeof(Vertex);geometry.Triangles.VertexCount=worldVB.SizeInBytes/sizeof(Vertex);geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.IndexBuffer=worldIB.BufferLocation;geometry.Triangles.IndexCount=worldIndexCount;geometry.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};bottom.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;bottom.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;bottom.NumDescs=1;bottom.pGeometryDescs=&geometry;bottom.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomSize{};device5->GetRaytracingAccelerationStructurePrebuildInfo(&bottom,&bottomSize);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};top.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;top.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;top.NumDescs=1;top.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topSize{};device5->GetRaytracingAccelerationStructurePrebuildInfo(&top,&topSize);
        if(!bottomSize.ResultDataMaxSizeInBytes||!topSize.ResultDataMaxSizeInBytes){error="Ray-tracing acceleration structure sizing failed";return false;}
        ComPtr<ID3D12Resource> scratch,instances;
        if(!createBuffer(bottomSize.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,blas,error,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
           !createBuffer(topSize.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,tlas,error,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
           !createBuffer(std::max(bottomSize.ScratchDataSizeInBytes,topSize.ScratchDataSizeInBytes),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,scratch,error,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
           !createBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,instances,error))return false;
        D3D12_RAYTRACING_INSTANCE_DESC instance{};instance.Transform[0][0]=1;instance.Transform[1][1]=1;instance.Transform[2][2]=1;instance.InstanceMask=255;instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;instance.AccelerationStructure=blas->GetGPUVirtualAddress();
        if(!writeUpload(instances.Get(),&instance,sizeof(instance),error)||!beginImmediate(error))return false;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=bottom;build.DestAccelerationStructureData=blas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();commands4->BuildRaytracingAccelerationStructure(&build,0,nullptr);uavBarrier(commands.Get(),blas.Get());uavBarrier(commands.Get(),scratch.Get());
        top.InstanceDescs=instances->GetGPUVirtualAddress();build.Inputs=top;build.DestAccelerationStructureData=tlas->GetGPUVirtualAddress();commands4->BuildRaytracingAccelerationStructure(&build,0,nullptr);uavBarrier(commands.Get(),tlas.Get());
        return endImmediate(error);
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
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;descriptors.NumDescriptors=FrameCount;
    if(!checked(p.device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&p.rtvHeap)),"Create render-target descriptors",error))return false;
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;descriptors.NumDescriptors=2;if(!checked(p.device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&p.dsvHeap)),"Create depth descriptor",error))return false;
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;descriptors.NumDescriptors=1;descriptors.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(!checked(p.device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&p.shadowHeap)),"Create shadow-map descriptor",error))return false;
    p.rtvStride=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return p.createTargets(error)&&p.createShadowTarget(error)&&p.createPipelines(error)&&p.checkDebugMessages(error);
}
bool Renderer::setWorld(const Mesh& mesh,std::string& error){
    auto& p=*impl;if(!p.device){error="Renderer is not initialized";return false;}
    if(mesh.vertices.size()>UINT_MAX/sizeof(Vertex)||mesh.indices.size()>UINT_MAX/sizeof(uint32_t)){error="World mesh exceeds Direct3D buffer view limits";return false;}
    if(!p.flush(error))return false;
    p.worldVertices.Reset();p.worldIndices.Reset();p.blas.Reset();p.tlas.Reset();p.worldIndexCount=0;
    if(mesh.vertices.empty()||mesh.indices.empty())return true;
    const uint64_t vertices=mesh.vertices.size()*sizeof(Vertex),indices=mesh.indices.size()*sizeof(uint32_t);
    ComPtr<ID3D12Resource> vertexUpload,indexUpload;
    if(!p.createBuffer(vertices,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,p.worldVertices,error)||!p.createBuffer(indices,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,p.worldIndices,error)||
       !p.createBuffer(vertices,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,vertexUpload,error)||!p.createBuffer(indices,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,indexUpload,error)||
       !p.writeUpload(vertexUpload.Get(),mesh.vertices.data(),size_t(vertices),error)||!p.writeUpload(indexUpload.Get(),mesh.indices.data(),size_t(indices),error)||!p.beginImmediate(error))return false;
    p.commands->CopyBufferRegion(p.worldVertices.Get(),0,vertexUpload.Get(),0,vertices);p.commands->CopyBufferRegion(p.worldIndices.Get(),0,indexUpload.Get(),0,indices);
    transition(p.commands.Get(),p.worldVertices.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ);transition(p.commands.Get(),p.worldIndices.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ);
    if(!p.endImmediate(error))return false;
    p.worldVB={p.worldVertices->GetGPUVirtualAddress(),UINT(vertices),sizeof(Vertex)};p.worldIB={p.worldIndices->GetGPUVirtualAddress(),UINT(indices),DXGI_FORMAT_R32_UINT};p.worldIndexCount=UINT(mesh.indices.size());
    if(p.raySupported&&!p.buildRayScene(error)){
        const std::string reason=error;
        // Only release acceleration-structure storage after outstanding GPU work
        // has completed. A removed or hung device remains a fatal rendering error.
        if(!p.flush(error))return false;
        p.raySupported=false;p.blas.Reset();p.tlas.Reset();
        const std::string warning="DXR unavailable; continuing with raster rendering: "+reason+"\n";
        OutputDebugStringA(warning.c_str());std::fputs(warning.c_str(),stderr);error.clear();
    }
    return p.checkDebugMessages(error);
}
bool Renderer::render(const RenderFrame& input,std::string& error){
    auto& p=*impl;if(!p.swapChain){error="Renderer is not initialized";return false;}
    const UINT current=p.swapChain->GetCurrentBackBufferIndex();auto& frame=p.frames[current];
    if(!p.wait(frame.fence,error))return false;
    const Mesh* dynamic=input.dynamic;size_t dynamicVertexBytes=dynamic?dynamic->vertices.size()*sizeof(Vertex):0,dynamicIndexBytes=dynamic?dynamic->indices.size()*sizeof(uint32_t):0,uiBytes=input.ui?input.ui->size()*sizeof(UiVertex):0;
    if(dynamicVertexBytes>UINT_MAX||dynamicIndexBytes>UINT_MAX||uiBytes>UINT_MAX){error="Frame geometry exceeds Direct3D buffer view limits";return false;}
    if(!p.ensureUpload(frame.dynamicVertices,frame.dynamicVertexCapacity,dynamicVertexBytes,error)||!p.ensureUpload(frame.dynamicIndices,frame.dynamicIndexCapacity,dynamicIndexBytes,error)||!p.ensureUpload(frame.ui,frame.uiCapacity,uiBytes,error))return false;
    if(dynamicVertexBytes&&!p.writeUpload(frame.dynamicVertices.Get(),dynamic->vertices.data(),dynamicVertexBytes,error))return false;
    if(dynamicIndexBytes&&!p.writeUpload(frame.dynamicIndices.Get(),dynamic->indices.data(),dynamicIndexBytes,error))return false;
    if(uiBytes&&!p.writeUpload(frame.ui.Get(),input.ui->data(),uiBytes,error))return false;
    Constants c{};const float fov=68*Pi/180,aspect=float(p.width)/float(p.height);
    c.viewProjection=multiply(lookAt(input.eye,input.target),perspective(fov,aspect,.12f,900));
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
    Mat4 lightView=lookAt(lightCenter+lightDirection*450.0f,lightCenter);
    Mat4 orthographic{};orthographic.m[0]=2/ShadowSpan;orthographic.m[5]=2/ShadowSpan;orthographic.m[10]=1/900.0f;orthographic.m[15]=1;
    c.lightProjection=multiply(lightView,orthographic);
    const float halfResolution=float(ShadowSize)*.5f;
    c.lightProjection.m[12]+=(std::round(c.lightProjection.m[12]*halfResolution)-c.lightProjection.m[12]*halfResolution)/halfResolution;
    c.lightProjection.m[13]+=(std::round(c.lightProjection.m[13]*halfResolution)-c.lightProjection.m[13]*halfResolution)/halfResolution;
    c.viewport[2]=1.0f/float(ShadowSize);c.viewport[3]=float(lightCount);
    std::memcpy(frame.mappedConstants,&c,sizeof(c));
    if(!checked(frame.allocator->Reset(),"Reset frame allocator",error)||!checked(p.commands->Reset(frame.allocator.Get(),nullptr),"Reset frame command list",error))return false;
    p.commands->SetGraphicsRootSignature(p.rootSignature.Get());p.commands->SetGraphicsRootConstantBufferView(0,frame.constants->GetGPUVirtualAddress());
    p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    transition(p.commands.Get(),p.shadowDepth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto shadowDSV=p.shadowDSV();p.commands->ClearDepthStencilView(shadowDSV,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);p.commands->OMSetRenderTargets(0,nullptr,FALSE,&shadowDSV);
    D3D12_VIEWPORT lightViewport{0,0,float(ShadowSize),float(ShadowSize),0,1};D3D12_RECT lightScissor{0,0,LONG(ShadowSize),LONG(ShadowSize)};
    p.commands->RSSetViewports(1,&lightViewport);p.commands->RSSetScissorRects(1,&lightScissor);p.commands->SetPipelineState(p.shadowPipeline.Get());
    if(p.worldIndexCount){p.commands->IASetVertexBuffers(0,1,&p.worldVB);p.commands->IASetIndexBuffer(&p.worldIB);p.commands->DrawIndexedInstanced(p.worldIndexCount,1,0,0,0);}
    if(dynamicVertexBytes&&dynamicIndexBytes){D3D12_VERTEX_BUFFER_VIEW vb{frame.dynamicVertices->GetGPUVirtualAddress(),UINT(dynamicVertexBytes),sizeof(Vertex)};D3D12_INDEX_BUFFER_VIEW ib{frame.dynamicIndices->GetGPUVirtualAddress(),UINT(dynamicIndexBytes),DXGI_FORMAT_R32_UINT};p.commands->IASetVertexBuffers(0,1,&vb);p.commands->IASetIndexBuffer(&ib);p.commands->DrawIndexedInstanced(UINT(dynamic->indices.size()),1,0,0,0);}
    transition(p.commands.Get(),p.shadowDepth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]={p.shadowHeap.Get()};p.commands->SetDescriptorHeaps(1,heaps);p.commands->SetGraphicsRootDescriptorTable(4,p.shadowHeap->GetGPUDescriptorHandleForHeapStart());
    transition(p.commands.Get(),p.backBuffers[current].Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv=p.rtvHeap->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=current*p.rtvStride;auto dsv=p.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    const float clear[]={.05f,.08f,.13f,1};p.commands->ClearRenderTargetView(rtv,clear,0,nullptr);p.commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    p.commands->OMSetRenderTargets(1,&rtv,FALSE,&dsv);D3D12_VIEWPORT viewport{0,0,float(p.width),float(p.height),0,1};D3D12_RECT scissor{0,0,LONG(p.width),LONG(p.height)};p.commands->RSSetViewports(1,&viewport);p.commands->RSSetScissorRects(1,&scissor);
    p.commands->SetGraphicsRootShaderResourceView(5,frame.lights->GetGPUVirtualAddress());
    if(ray){p.commands->SetGraphicsRootShaderResourceView(1,p.tlas->GetGPUVirtualAddress());p.commands->SetGraphicsRootShaderResourceView(2,p.worldVertices->GetGPUVirtualAddress());p.commands->SetGraphicsRootShaderResourceView(3,p.worldIndices->GetGPUVirtualAddress());}
    p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);p.commands->SetPipelineState(p.skyPipeline.Get());p.commands->DrawInstanced(3,1,0,0);
    p.commands->SetPipelineState(ray?p.rayPipeline.Get():p.worldPipeline.Get());
    if(p.worldIndexCount){p.commands->IASetVertexBuffers(0,1,&p.worldVB);p.commands->IASetIndexBuffer(&p.worldIB);p.commands->DrawIndexedInstanced(p.worldIndexCount,1,0,0,0);}
    if(dynamicVertexBytes&&dynamicIndexBytes){D3D12_VERTEX_BUFFER_VIEW vb{frame.dynamicVertices->GetGPUVirtualAddress(),UINT(dynamicVertexBytes),sizeof(Vertex)};D3D12_INDEX_BUFFER_VIEW ib{frame.dynamicIndices->GetGPUVirtualAddress(),UINT(dynamicIndexBytes),DXGI_FORMAT_R32_UINT};p.commands->IASetVertexBuffers(0,1,&vb);p.commands->IASetIndexBuffer(&ib);p.commands->DrawIndexedInstanced(UINT(dynamic->indices.size()),1,0,0,0);}
    if(uiBytes){p.commands->SetPipelineState(p.uiPipeline.Get());D3D12_VERTEX_BUFFER_VIEW vb{frame.ui->GetGPUVirtualAddress(),UINT(uiBytes),sizeof(UiVertex)};p.commands->IASetVertexBuffers(0,1,&vb);p.commands->DrawInstanced(UINT(input.ui->size()),1,0,0);}
    transition(p.commands.Get(),p.backBuffers[current].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
    if(!checked(p.commands->Close(),"Close frame command list",error))return false;ID3D12CommandList* lists[]={p.commands.Get()};p.queue->ExecuteCommandLists(1,lists);
    HRESULT presented=p.swapChain->Present(input.vsync?1:0,(!input.vsync&&p.tearing)?DXGI_PRESENT_ALLOW_TEARING:0);
    frame.fence=p.nextFence++;if(!checked(p.queue->Signal(p.fence.Get(),frame.fence),"Signal frame completion",error))return false;
    if(!checked(presented,"Present frame",error)){HRESULT removed=p.device->GetDeviceRemovedReason();if(FAILED(removed)){char reason[64]{};std::snprintf(reason,sizeof(reason)," Device removed: 0x%08lX",static_cast<unsigned long>(removed));error+=reason;}return false;}
    p.lastPresented=current;p.hasPresented=true;++p.totalFrames;return p.checkDebugMessages(error);
}
bool Renderer::resize(uint32_t width,uint32_t height,std::string& error){
    auto& p=*impl;if(!p.swapChain||!width||!height||(p.width==width&&p.height==height))return true;
    if(!p.flush(error))return false;for(auto& buffer:p.backBuffers)buffer.Reset();p.depth.Reset();
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
