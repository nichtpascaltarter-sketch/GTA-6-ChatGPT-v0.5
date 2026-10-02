#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
namespace mc {
enum class FootSurface { Pavement, Soil, Grass, Sand, Wood };
inline constexpr unsigned WorldEngineSources=8,WorldFootSources=8;
struct WorldEngineSound {
    // IDs are stable per live entity generation. Gains include distance/pan.
    std::uint64_t id=0;
    float left=0,right=0,speed=0,throttle=0;
    int kind=0;
};
struct WorldFootSound {
    // Retain each selected walker between strikes. A new admission primes its
    // serial; only a later serial can start a contact. Side is 0 or 1.
    std::uint64_t id=0,strikeSerial=0;
    // A latest strike up to 0.12 seconds old may sound; older ones are consumed
    // without catch-up. Strength is 0..1; existing tails retain their material.
    float left=0,right=0,strikeAgeSeconds=0,strength=0;
    FootSurface surface=FootSurface::Pavement;
    int side=0;
};
struct WorldAudioState {
    // Change epoch on load/reset. IDs must change when an entity is recycled.
    // Publish a strictly increasing serial once per new tracker snapshot,
    // including stationary/cinematic heartbeats. Re-reading is not a heartbeat.
    std::uint64_t epoch=0,publicationSerial=0;
    std::array<WorldEngineSound,WorldEngineSources> engines{};
    std::array<WorldFootSound,WorldFootSources> feet{};
    unsigned engineCount=0,footCount=0;
    // False prevents new foot contacts while allowing stationary engine sound.
    bool advancing=true;
};
struct AudioState {
    float speed=0,engine=0,rain=0,wanted=0,shot=0;
    int station=1;float volume=.65f;bool paused=false;
    // Engine kinds follow car, motorcycle, boat, and light aircraft.
    int engineKind=0;float throttle=0,shore=0,nature=0,urban=0;
    // Actual movement drives contact sounds; no foot contacts in a vehicle or air.
    FootSurface footSurface=FootSurface::Pavement;
    float footSpeed=0,waterMotion=0,tireScrub=0;
    bool footContact=false;
    WorldAudioState world{};
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
