#pragma once
#include <memory>
#include <string>
namespace mc {
enum class FootSurface { Pavement, Soil, Grass, Sand, Wood };
struct AudioState {
    float speed=0,engine=0,rain=0,wanted=0,shot=0;
    int station=1;float volume=.65f;bool paused=false;
    // Engine kinds follow car, motorcycle, boat, and light aircraft.
    int engineKind=0;float throttle=0,shore=0,nature=0,urban=0;
    // Actual movement drives contact sounds; no foot contacts in a vehicle or air.
    FootSurface footSurface=FootSurface::Pavement;
    float footSpeed=0,waterMotion=0,tireScrub=0;
    bool footContact=false;
};
class Audio {
public:
    Audio();~Audio();
    bool initialize(std::string& error);
    void update(const AudioState& state);
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
