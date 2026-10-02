#include "../src/game.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mc;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool near(Vec3 a,Vec3 b,float tolerance=.0001f){return length(a-b)<tolerance;}
Game fixture(){
    Game game;game.player={0,game.world.height(0,0),0};game.health=80;game.world.stream(game.player);
    game.pedestrians.resize(4);for(auto& p:game.pedestrians){p.health=0;p.position={1000,0,1000};}
    game.pedestrians[0].health=100;game.pedestrians[0].position={0,game.world.height(0,16),16};game.pedestrians[0].yaw=Pi;
    return game;
}
void tick(Game& game){game.update({},1.f/60,false);}
void validate(const Mesh& mesh){
    require(!mesh.vertices.empty()&&mesh.indices.size()%3==0,"invalid visual mesh cardinality");
    for(auto index:mesh.indices)require(index<mesh.vertices.size(),"visual index outside vertex range");
    for(const auto& v:mesh.vertices){require(std::isfinite(length(v.position))&&std::isfinite(length(v.normal)),"nonfinite police geometry");require(std::abs(length(v.normal)-1)<.003f,"police geometry normal is not unit length");}
}
size_t count(const Mesh& mesh,Vec3 color){size_t total=0;for(const auto& v:mesh.vertices)if(near(v.color,color))++total;return total;}
Vec3 barrelCenter(const Mesh& mesh){Vec3 center{};size_t vertices=0;for(const auto& v:mesh.vertices)if(near(v.color,{.049f,.053f,.061f})){center+=v.position;++vertices;}require(vertices==24,"officer barrel missing");return center/float(vertices);}
size_t flashVertices(const Mesh& mesh){size_t total=0;for(const auto& v:mesh.vertices)if(v.material==5&&(near(v.color,{1,.73f,.22f})||near(v.color,{1,.68f,.23f})))++total;return total;}
void alignmentAndEvents(){
    auto game=fixture();game.wanted=1;
    for(int i=0;i<20;++i){tick(game);require(game.lawShots().empty()&&game.policePose(0).flash==0,"premature visual shot during observation");require(flashVertices(game.dynamicMesh())==0,"wanted level created a magic muzzle event");}
    bool sawShot=false,sawAim=false,sawReload=false,sawMagazine=false,sawFinishedReload=false;unsigned sampled=0;Vec3 lastReloadBarrel{};
    for(int frame=0;frame<700;++frame){
        tick(game);const auto pose=game.policePose(0);
        if(!game.lawShots().empty()){
            sawShot=true;require(pose.flash>0&&near(pose.muzzle,game.lawShots()[0].origin),"visual muzzle does not belong to emitted shot");
            const auto mesh=game.dynamicMesh();validate(mesh);require(flashVertices(mesh)>0,"emitted shot has no visual event");
            bool atImpact=false;for(const auto& vertex:mesh.vertices)if(vertex.material==5&&length(vertex.position-pose.impact)<.02f)atImpact=true;
            require(atImpact,"tracer missed the physically resolved endpoint");
            bool lit=false;for(const auto& light:game.lightSources())if(near(light.position,pose.muzzle)&&near(light.color,{1,.73f,.22f})&&light.intensity>0)lit=true;
            require(lit,"actual shot did not create its short local light");
        }
        if(pose.aim>.999f&&pose.flash==0&&!pose.reloading&&!sawAim){
            sawAim=true;
            for(float range:{16.f,60.f}){
                auto view=game;view.player=view.pedestrians[0].position+Vec3{range,0,0};const auto mesh=view.dynamicMesh();validate(mesh);
                const Vec3 direction=normalized(pose.aimPoint-(game.pedestrians[0].position+Vec3{0,1.35f,0}));
                float nearest=100,mostForward=-100;size_t barrel=0;
                for(const auto& vertex:mesh.vertices)if(near(vertex.color,{.049f,.053f,.061f})){
                    ++barrel;nearest=std::min(nearest,length(vertex.position-pose.muzzle));mostForward=std::max(mostForward,dot(vertex.position-pose.muzzle,direction));
                }
                require(barrel==24&&nearest<.065f&&std::abs(mostForward)<.002f,"authored barrel tip not aligned with physical muzzle");
                require(flashVertices(mesh)==0,"held aim emitted continuous tracer geometry");++sampled;
            }
        }
        if(pose.reloading){
            sawReload=true;lastReloadBarrel=barrelCenter(game.dynamicMesh())-game.pedestrians[0].position;
            if(pose.reload>.32f&&pose.reload<.65f){
                const auto mesh=game.dynamicMesh();validate(mesh);require(count(mesh,{.10f,.12f,.14f})==24,"reload does not show held magazine");
                require(flashVertices(mesh)==0,"reload fabricated a shot event");
                Vec3 center{};size_t vertices=0;for(const auto& v:mesh.vertices)if(near(v.color,{.10f,.12f,.14f})){center+=v.position;++vertices;}
                center=center/float(vertices);require(center.y<game.pedestrians[0].position.y+1.2f,"reload hand never reaches lower magazine/pouch area");
                sawMagazine=true;++sampled;
            }
        }else if(sawReload){
            sawFinishedReload=true;const auto mesh=game.dynamicMesh();
            require(length(barrelCenter(mesh)-game.pedestrians[0].position-lastReloadBarrel)<.08f,"reload completion popped the gun before its aim ramp");
            require(count(mesh,{.10f,.12f,.14f})==0,"loose magazine survived completed reload");break;
        }
    }
    require(sawShot&&sawAim&&sawReload&&sawMagazine&&sawFinishedReload,"actual officer sequence did not cover aim, shot and complete reload");
    std::cout<<"PASS real aim/fire/reload sequence, physical muzzle/impact alignment, near/far mesh validity and contact props ("<<sampled<<" samples)\n";
}
void blockedAndPaused(){
    auto blocked=fixture();blocked.wanted=1;blocked.world.chunks.front().solids.push_back({{-100,-2,4},{100,5,8}});
    for(int i=0;i<120;++i){tick(blocked);require(blocked.lawShots().empty()&&blocked.policePose(0).flash==0,"occluded officer emitted an earlier hidden shot");}
    require(flashVertices(blocked.dynamicMesh())==0,"occluded officer produced tracer");
    auto game=fixture();game.wanted=1;for(int i=0;i<180&&game.lawShots().empty();++i)tick(game);
    require(!game.lawShots().empty()&&flashVertices(game.dynamicMesh())>0,"pause test missed actual shot");
    game.paused=true;tick(game);require(game.lawShots().empty()&&game.policePose(0).flash==0&&flashVertices(game.dynamicMesh())==0,"pause retained muzzle/tracer event");
    game.paused=false;game.pedestrians[0].health=0;tick(game);require(flashVertices(game.dynamicMesh())==0,"dead officer retained tracer");
    std::cout<<"PASS blocked, paused and dead officers emit no invented tracer or muzzle event\n";
}
}
int main(){try{alignmentAndEvents();blockedAndPaused();return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
