#include "../../../src/police_scenes.h"
#include <iostream>
int main(){
    for(const std::string scene:{"police-aim","police-fire","police-reload"}){
        mc::Game game;game.initialize();mc::PoliceCapture capture;std::string error;
        const auto people=game.pedestrians.size(),vehicles=game.vehicles.size();
        if(!capture.prepare(game,scene,std::cout,error,[]{return true;})){std::cerr<<scene<<": "<<error<<'\n';return 1;}
        if(!capture.active||!game.paused||game.pedestrians.size()!=people||game.vehicles.size()!=vehicles)return 2;
        const auto pose=game.policePose(0);
        if(scene=="police-fire"&&(game.lawShots().empty()||pose.flash<=0))return 3;
        for(int frame=0;frame<8;++frame){
            const auto mesh=game.dynamicMesh();const auto lights=game.lightSources();
            if(mesh.indices.empty()||lights.empty()||!capture.retained(game))return 4;
        }
        game.update({},1.f/60,false);
        if(scene!="police-reload"&&capture.retained(game))return 5;
    }
    return 0;
}
