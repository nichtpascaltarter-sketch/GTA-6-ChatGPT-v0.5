#include "game.h"
#include <cstdio>
#include <cmath>
int main(){
    for(float release:{18.f,18.1f,18.2f,20.f}){
        mc::Game game;game.initialize();int index=-1;
        for(size_t i=0;i<game.vehicles.size();++i)if(game.vehicles[i].kind==mc::VehicleKind::Aircraft)index=int(i);
        const float ground=game.world.height(-3200,-1000);auto& craft=game.vehicles[size_t(index)];
        craft.position={-3200,ground+release,-1000};craft.speed=0;craft.velocity={};craft.throttle=0;craft.health=100;craft.pitch=craft.roll=craft.steer=0;craft.parked=false;
        game.occupied=-1;game.player={-3214,game.world.height(-3214,-1000),-1000};game.world.stream(game.player);
        std::printf("RELEASE %.3f ground %.3f\n",release,ground);
        for(int frame=0;frame<240;++frame){
            float beforeAltitude=craft.position.y-ground,beforeVelocity=craft.velocity.y,beforeHealth=craft.health;
            game.update({},1.f/60);
            float altitude=craft.position.y-ground;
            if(beforeAltitude<.7f&&beforeAltitude>0)std::printf("frame=%d altitude=%.9f->%.9f vy=%.9f->%.9f health=%.3f->%.3f parked=%d\n",frame,beforeAltitude,altitude,beforeVelocity,craft.velocity.y,beforeHealth,craft.health,craft.parked);
            if(craft.parked&&altitude<.01f)break;
        }
        std::printf("FINAL health %.3f altitude %.9f\n",craft.health,craft.position.y-ground);
    }
}
