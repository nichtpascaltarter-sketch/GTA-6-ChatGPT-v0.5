#pragma once
#include "world.h"
#include <string>
#include <memory>
#include <vector>
#include <array>

namespace mc {
struct UiVertex {float x,y,r,g,b,a;};
struct RenderFrame {
    Vec3 eye,target;float time=0,dayTime=17,rain=0;
    bool rayTracing=true,vsync=true;float exposure=1;
    // Coverage is measured at eye.xz; negative retains the legacy short fade.
    // Ground height keeps the directional shadow volume near visible terrain.
    float coverageRadius=-1.0f,groundHeight=0.0f;
    const Mesh* dynamic=nullptr;
    const std::vector<Light>* lights=nullptr;
    const std::vector<UiVertex>* ui=nullptr;
};
struct StreamStats {
    uint64_t epoch=0;
    uint32_t residentChunks=0,pendingBatches=0;
    uint64_t residentBytes=0,vertexArenaBytes=0,indexArenaBytes=0;
    uint64_t pendingUploadBytes=0,retiredBytes=0;
    uint64_t uploadedChunks=0,retainedChunks=0,retiredChunks=0,blasBuilds=0;
    uint64_t ordinaryWaits=0,pressureWaits=0,repackWaits=0;
    uint64_t renderRevision=0,tlasBuilds=0;
    std::array<uint32_t,3> residentTilesByLod{};
    std::array<uint64_t,3> residentBytesByLod{},uploadedTilesByLod{};
    uint32_t rayInstances=0;
    uint32_t mainDrawn=0,mainCulled=0,shadowDrawn=0,shadowCulled=0;
    float fogStart=260.0f,fogEnd=440.0f;
};
class Renderer {
public:
    Renderer();~Renderer();
    bool initialize(void* window,uint32_t width,uint32_t height,std::string& error,bool warp=false);
    // Chunk coordinates identify immutable geometry within an epoch. Advance
    // epoch after reset/load. Mesh data is copied before this call returns.
    bool setWorld(const World&,uint64_t epoch,std::string& error);
    StreamStats streamStats() const;
    bool render(const RenderFrame&,std::string& error);
    bool resize(uint32_t width,uint32_t height,std::string& error);
    bool rayTracingAvailable() const;
    const char* adapterName() const;
    uint64_t frameCount() const;
    bool capture(const std::string& path,std::string& error);
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
