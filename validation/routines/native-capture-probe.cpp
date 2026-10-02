#include "resident_scenes.h"
#include <iostream>

int main(){
    for(const char* scene:{"residents-carry","residents-work","residents-bench","residents-talk","residents-startle","residents-flee"}){
        mc::Game game;game.initialize();mc::ResidentCapture capture;std::string error;
        if(!capture.prepare(game,scene,std::cout,error,[]{return true;})){
            std::cerr<<scene<<": "<<error<<'\n';return 1;
        }
        if(!capture.active||!game.paused||game.pedestrians.size()!=84||game.world.blocked(game.player,.35f))return 2;
        if(!mc::ResidentCapture::clear(game.world,capture.target+mc::Vec3{0,.55f,0},capture.eye))return 3;
    }
    mc::Game game;game.initialize();mc::ResidentCapture invalid;std::string error;
    if(invalid.prepare(game,"residents-unknown",std::cout,error,[]{return true;})||invalid.active)return 4;
    if(error!="Unknown resident inspection scene.")return 5;
    error.clear();
    if(invalid.prepare(game,"residents-work",std::cout,error,[]{return false;})||invalid.active)return 6;
    if(error!="Resident scene interrupted while advancing simulation.")return 7;
    std::cout<<"All six natural resident captures and cancellation checks passed.\n";
}
