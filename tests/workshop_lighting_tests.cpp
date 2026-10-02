#include "../src/game.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace mc;
namespace {
const Light* findLight(const std::vector<Light>& lights,Vec3 position){
    for(const auto& light:lights)if(length(light.position-position)<.0001f)return &light;
    return nullptr;
}
bool close(float a,float b){return std::abs(a-b)<.0001f;}
void poweredFixturesIgnoreDaylight(){
    Game game;game.player={0,0,0};game.world.chunks.emplace_back();
    Light fixture;fixture.position={2,5,1};fixture.color={.9f,.96f,1};fixture.intensity=65;
    fixture.direction={0,-3,0};fixture.cone=-.15f;
    Light street=fixture;street.position={8,7,2};street.intensity=100;
    game.world.chunks.front().alwaysLights.push_back(fixture);
    game.world.chunks.front().lights.push_back(street);
    for(float hour:{0.f,6.f,12.f,17.2f,18.f,23.f}){
        game.dayTime=hour;
        for(float rain:{0.f,1.f}){
            game.rain=rain;const auto selected=game.lightSources();
            const Light* active=findLight(selected,fixture.position);assert(active);
            assert(close(active->intensity,fixture.intensity)&&close(active->radius,fixture.radius));
            assert(length(active->color-fixture.color)<.0001f&&close(active->cone,fixture.cone));
            assert(length(active->direction-Vec3{0,-1,0})<.0001f);
            if(hour==12)assert(selected.size()==1&&!findLight(selected,street.position));
            if(hour==0){const Light* lamp=findLight(selected,street.position);assert(lamp&&close(lamp->intensity,street.intensity));}
        }
    }
}
void sharedValidationAndBudget(){
    Game game;game.player={0,0,0};game.dayTime=12;game.world.chunks.emplace_back();
    auto& chunk=game.world.chunks.front();Light valid;valid.position={0,5,0};
    chunk.alwaysLights.push_back(valid);
    auto invalid=valid;invalid.radius=0;chunk.alwaysLights.push_back(invalid);
    invalid=valid;invalid.intensity=std::numeric_limits<float>::infinity();chunk.alwaysLights.push_back(invalid);
    invalid=valid;invalid.position.x=std::numeric_limits<float>::quiet_NaN();chunk.alwaysLights.push_back(invalid);
    invalid=valid;invalid.cone=1;chunk.alwaysLights.push_back(invalid);
    invalid=valid;invalid.color.x=-1;chunk.alwaysLights.push_back(invalid);
    invalid=valid;invalid.position.x=1000;chunk.alwaysLights.push_back(invalid);
    assert(game.lightSources().size()==1);
    chunk.alwaysLights.clear();game.dayTime=0;
    for(int i=0;i<10;++i){Light fixture;fixture.position={float(i%5)-2,5,float(i/5)};fixture.intensity=50.f+float(i);chunk.alwaysLights.push_back(fixture);}
    for(int i=0;i<100;++i){Light street;street.position={30.f+float(i%10),7,30.f+float(i/10)};chunk.lights.push_back(street);}
    const auto first=game.lightSources(),second=game.lightSources();assert(first.size()==64&&second.size()==64);
    for(const auto& source:chunk.alwaysLights){const Light* selected=findLight(first,source.position);assert(selected&&close(selected->intensity,source.intensity));}
    for(size_t i=0;i<first.size();++i){assert(length(first[i].position-second[i].position)<.0001f);assert(close(first[i].intensity,second[i].intensity));}
}
void authoredWorkshopFixtures(){
    const auto site=World::garageSite();Game game;game.player=site.vehicleStop;assert(game.world.stream(game.player));
    std::vector<Light> fixtures;size_t luminousVertices=0;
    for(const auto& chunk:game.world.chunks){
        fixtures.insert(fixtures.end(),chunk.alwaysLights.begin(),chunk.alwaysLights.end());
        for(const auto& vertex:chunk.mesh.vertices)if(vertex.material>4.5f&&vertex.material<5.5f)++luminousVertices;
    }
    assert(!fixtures.empty()&&fixtures.size()<=64&&luminousVertices>0);
    // Being selected is insufficient: the authored cones must actually reach
    // the service floor, the car roof and the customer counter, away from the
    // outer cone edge. This catches narrow pools placed beside the useful area.
    for(Vec3 receiver:{site.vehicleStop+Vec3{0,.05f,0},site.vehicleStop+Vec3{0,1.5f,0},site.counter+Vec3{0,.8f,0}}){
        bool reached=false;
        for(const auto& light:fixtures){
            const Vec3 ray=receiver-light.position;
            if(length(ray)<light.radius&&dot(normalized(ray),normalized(light.direction))>light.cone+.02f)reached=true;
        }
        assert(reached);
    }
    for(Vec3 observer:{site.vehicleStop,site.counter,site.marker}){
        game.player=observer;
        for(float hour:{0.f,12.f}){
            game.dayTime=hour;const auto selected=game.lightSources();assert(selected.size()<=64);
            for(const auto& source:fixtures){
                const Light* light=findLight(selected,source.position);assert(light);
                assert(close(light->intensity,source.intensity)&&length(light->color-source.color)<.0001f);
                assert(light->intensity*std::max({light->color.x,light->color.y,light->color.z})>=.5f);
            }
        }
    }
    std::printf("Authored workshop: %zu powered lights retained at bay, counter and approach by day/night; %zu luminous vertices.\n",fixtures.size(),luminousVertices);
}
void nearbyAttendant(){
    const auto site=World::garageSite();Game game;game.player=site.counter;
    auto staffVertices=[&](const Mesh& mesh){
        size_t count=0;
        for(const auto& vertex:mesh.vertices){const Vec3 relative=vertex.position-site.staff;
            if(std::abs(relative.x)<.8f&&std::abs(relative.z)<.8f&&relative.y>=-.05f&&relative.y<2)++count;
        }
        return count;
    };
    assert(staffVertices(game.dynamicMesh())>300);
    game.player=site.staff+Vec3{150,0,0};assert(staffVertices(game.dynamicMesh())==0);
}
}
int main(){
    poweredFixturesIgnoreDaylight();sharedValidationAndBudget();authoredWorkshopFixtures();nearbyAttendant();
    std::puts("Workshop lighting: constant fixture energy, ordinary night scaling, weather independence, input validation, range culling, stable 64-light selection, useful beam coverage and nearby attendant passed.");
}
