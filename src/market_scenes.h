#pragma once
#include "game.h"
#include <array>
#include <ostream>

namespace mc {
struct MarketCapture {
    struct View {const char* name;Vec3 eye,target;float hour;};
    static constexpr std::array<View,5> views{{
        {"market-day",{28,15,30},{65,3.5f,68},14},
        {"market-night",{37,3.8f,26},{75,3.5f,57},23},
        {"market-citrus",{81.8f,2.35f,31.9f},{87.35f,1.38f,35},14},
        {"market-tea",{81.8f,2.35f,50.9f},{87.35f,1.38f,54},14},
        {"market-bread",{81.8f,2.35f,69.9f},{87.35f,1.38f,73},14}
    }};
    Vec3 eye{},target{};
    bool active=false;
    bool prepare(Game& game,const std::string& scene,std::ostream& log,std::string& error){
        active=false;
        for(const auto& view:views)if(scene==view.name){
            // Preserve the full initialized population and authored market state.
            game.player={77,0,17};game.player.y=game.world.height(game.player.x,game.player.z);
            game.world.stream(game.player);game.dayTime=view.hour;game.rain=0;
            game.paused=true;game.messageTime=0;eye=view.eye;target=view.target;
            if(game.world.blocked(game.player,.35f)||game.world.blocked(eye,.1f)){
                error="Market inspection camera or spectator is obstructed.";return false;
            }
            active=true;
            log<<"Market capture: scene="<<scene<<"; hour="<<view.hour<<"; people="<<game.pedestrians.size()
               <<"; eye="<<eye.x<<','<<eye.y<<','<<eye.z<<"; target="<<target.x<<','<<target.y<<','<<target.z<<'\n';
            return true;
        }
        error="Unknown market inspection scene.";return false;
    }
};
}
