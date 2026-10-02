#pragma once
#include "audio.h"
#include <mutex>

namespace mc {
// The render worker never waits for the simulation publisher. An unpublished
// mailbox leaves its caller's silent fallback unchanged, including on startup.
class AudioMailbox {
public:
    void publish(const AudioState& value) {
        std::lock_guard<std::mutex> guard(mutex_);
        state_=value;published_=true;
    }
    bool tryRead(AudioState& value) {
        std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock);
        if(!guard.owns_lock()||!published_)return false;
        value=state_;return true;
    }
private:
    std::mutex mutex_;
    AudioState state_{};
    bool published_=false;
};
}
