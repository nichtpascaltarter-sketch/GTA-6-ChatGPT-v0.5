#include "resident_scenes.h"
#include "market_scenes.h"
#include <sstream>
#include <array>
#include <cassert>
#include <cstdio>

using namespace mc;
bool close(float a,float b){return std::abs(a-b)<.001f;}
using View=MarketCapture::View;
bool framed(const View& view,Vec3 p){
    const Vec3 forward=normalized(view.target-view.eye),right=normalized(cross({0,1,0},forward)),up=cross(forward,right),offset=p-view.eye;
    const float depth=dot(offset,forward);if(depth<.12f)return false;
    const float vertical=std::tan(68*Pi/360)*depth;
    return std::abs(dot(offset,up))<vertical*.94f&&std::abs(dot(offset,right))<vertical*(16.f/9)*.94f;
}
int main(){
    size_t errors=0;
    const auto& views=MarketCapture::views;
    for(size_t index=0;index<views.size();++index){
        const auto& view=views[index];Game game;game.initialize();
        const auto people=game.pedestrians;const auto vehicles=game.vehicles;
        MarketCapture capture;std::ostringstream log;std::string error;
        assert(capture.prepare(game,view.name,log,error));
        assert(capture.active&&game.paused&&game.dayTime==view.hour&&game.rain==0);
        assert(length(capture.eye-view.eye)==0&&length(capture.target-view.target)==0);
        assert(people.size()==game.pedestrians.size()&&vehicles.size()==game.vehicles.size());
        for(size_t i=0;i<people.size();++i)assert(length(people[i].position-game.pedestrians[i].position)==0&&people[i].identity==game.pedestrians[i].identity);
        for(size_t i=0;i<vehicles.size();++i)assert(length(vehicles[i].position-game.vehicles[i].position)==0&&vehicles[i].identity==game.vehicles[i].identity);
        assert(!game.world.blocked(game.player,.35f));assert(!game.world.blocked(view.eye,.10f));
        const auto dynamic=game.dynamicMesh();
        size_t points=0;
        const auto inspect=[&](Vec3 sample){
            const bool staticClear=ResidentCapture::clearGeometry(game.world,sample,view.eye),dynamicClear=ResidentCapture::clearMesh(dynamic,sample,view.eye,.02f);
            const bool visible=staticClear&&dynamicClear;
            if(!visible||!framed(view,sample))std::fprintf(stderr,"%s rejected point %.3f %.3f %.3f (static=%d dynamic=%d framed=%d)\n",view.name,sample.x,sample.y,sample.z,int(staticClear),int(dynamicClear),int(framed(view,sample)));
            if(!visible||!framed(view,sample))++errors;
            ++points;
        };
        if(index<2){
            inspect({83.43f,5.30f,54});
            for(float z:{35.f,54.f,73.f})inspect({83.48f,4.53f,z});
            // Sample the visible faces, not points inside clock hands or just
            // above a pole whose own top hides that point from a lower camera.
            inspect({44,13,94.10f});inspect({44,27.68f,97.84f});
            inspect({64.28f,7.44f,62.28f});
        }else if(index==2){
            for(float z:{33.5f,35.f,36.5f})inspect({86.97f,1.62f,z});
            inspect({86.34f,1.16f,32.7f});inspect({86.34f,1.16f,37.3f});
        }else if(index==3){
            inspect({87.22f,1.87f,54.35f});inspect({86.84f,1.36f,53.35f});inspect({86.84f,1.36f,55.15f});
            inspect({86.34f,1.16f,51.7f});inspect({86.34f,1.16f,56.3f});
        }else{
            inspect({86.92f,1.47f,71.9f});inspect({86.92f,1.47f,74.1f});
            inspect({86.34f,1.16f,70.7f});inspect({86.34f,1.16f,75.3f});
        }
        size_t marketLights=0;for(const auto& light:game.lightSources())if(close(light.position.x,84.45f)&&close(light.position.y,4.48f))++marketLights;
        assert(marketLights==(view.hour==23?4u:0u));
        std::printf("%s: eye %.3f,%.3f,%.3f target %.3f,%.3f,%.3f hour %.0f; %zu framed samples clear of static/dynamic triangles; %zu active market lights; %zu people\n",
            view.name,view.eye.x,view.eye.y,view.eye.z,view.target.x,view.target.y,view.target.z,view.hour,points,marketLights,game.pedestrians.size());
    }
    Game untouched;MarketCapture invalid;std::ostringstream log;std::string error;
    assert(!invalid.prepare(untouched,"market-unknown",log,error)&&!invalid.active);
    assert(error=="Unknown market inspection scene.");
    return errors?1:0;
}
