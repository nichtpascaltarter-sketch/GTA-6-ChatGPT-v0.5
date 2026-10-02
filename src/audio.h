#pragma once
#include <memory>
#include <string>
namespace mc {
struct AudioState {float speed=0;float engine=0;float rain=0;float wanted=0;float shot=0;int station=1;float volume=.65f;bool paused=false;};
class Audio {
public:
    Audio();~Audio();
    bool initialize(std::string& error);
    void update(const AudioState& state);
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
