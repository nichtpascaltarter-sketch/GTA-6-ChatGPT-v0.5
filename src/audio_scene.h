#pragma once
#include "audio.h"
#include "game.h"

namespace mc {
inline void movementAudio(AudioState& state,const Game& game,Vec3 previousPosition,
                          int previousVehicle,const Input& input,float dt) {
    state.footSpeed=state.waterMotion=state.tireScrub=0;state.footContact=false;
    if(game.paused||!std::isfinite(dt)||dt<=0)return;
    if(game.occupied>=0) {
        if(size_t(game.occupied)>=game.vehicles.size())return;
        const auto& vehicle=game.vehicles[size_t(game.occupied)];
        if(vehicle.kind==VehicleKind::Car||vehicle.kind==VehicleKind::Motorcycle) {
            const float slip=std::abs(dot(vehicle.velocity,right(vehicle.yaw)));
            state.tireScrub=clamp((slip-1.2f)/7.f+(input.brake?std::abs(vehicle.speed)/35.f:0.f),0,1);
        }
        return;
    }
    if(previousVehicle>=0)return;
    const float speed=std::hypot(game.player.x-previousPosition.x,game.player.z-previousPosition.z)/dt;
    // Respawns and position corrections must not sound like a burst of footsteps.
    if(!std::isfinite(speed)||speed>10)return;
    const float depth=game.world.waterDepth(game.player.x,game.player.z);
    if(depth>.05f&&game.player.y<World::WaterLevel+.25f) {
        state.waterMotion=clamp(speed/3.4f,0,1);return;
    }
    state.footSpeed=speed;
    state.footContact=std::abs(game.player.y-game.world.height(game.player.x,game.player.z))<.06f;
    switch(game.world.groundSurface(game.player.x,game.player.z)) {
    case GroundSurface::Pavement:state.footSurface=FootSurface::Pavement;break;
    case GroundSurface::Soil:state.footSurface=FootSurface::Soil;break;
    case GroundSurface::Grass:state.footSurface=FootSurface::Grass;break;
    case GroundSurface::Sand:state.footSurface=FootSurface::Sand;break;
    case GroundSurface::Wood:state.footSurface=FootSurface::Wood;break;
    }
}
}
