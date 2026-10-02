#pragma once
#include "world.h"
#include <string>
#include <memory>
#include <vector>

namespace mc {
struct UiVertex {float x,y,r,g,b,a;};
struct RenderFrame {
    Vec3 eye,target;float time=0,dayTime=17,rain=0;
    bool rayTracing=true,vsync=true;float exposure=1;
    const Mesh* dynamic=nullptr;
    const std::vector<Light>* lights=nullptr;
    const std::vector<UiVertex>* ui=nullptr;
};
class Renderer {
public:
    Renderer();~Renderer();
    bool initialize(void* window,uint32_t width,uint32_t height,std::string& error,bool warp=false);
    bool setWorld(const Mesh&,std::string& error);
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
