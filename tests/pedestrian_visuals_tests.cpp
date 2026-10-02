#include "../src/game.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace mc;
namespace {
bool same(Vec3 a,Vec3 b,float tolerance=.0001f){return length(a-b)<=tolerance;}
Game actor(PedestrianActivity activity){
    Game game;game.player={10,0,0};game.pedestrians.resize(5);
    for(auto& person:game.pedestrians)person.position={1000,0,1000};
    auto& person=game.pedestrians.back();person.position={0,0,0};person.identity=173;
    person.activity=activity;person.phase=.7f;person.motion=0;
    return game;
}
std::vector<Vertex> nearby(const Game& game,Vec3 anchor,float below=10){
    const Mesh mesh=game.dynamicMesh();std::vector<Vertex> result;
    for(uint32_t index:mesh.indices)assert(index<mesh.vertices.size());
    for(const auto& vertex:mesh.vertices){
        assert(std::isfinite(vertex.position.x)&&std::isfinite(vertex.position.y)&&std::isfinite(vertex.position.z));
        assert(std::isfinite(vertex.normal.x)&&std::isfinite(vertex.normal.y)&&std::isfinite(vertex.normal.z));
        assert(std::abs(length(vertex.normal)-1)<.002f);
        if(std::abs(vertex.position.x-anchor.x)<2.5f&&std::abs(vertex.position.z-anchor.z)<2.5f&&vertex.position.y<below)result.push_back(vertex);
    }
    assert(!result.empty());return result;
}
bool sameGeometry(const std::vector<Vertex>& a,const std::vector<Vertex>& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(!same(a[i].position,b[i].position)||!same(a[i].normal,b[i].normal)||!same(a[i].color,b[i].color))return false;
    return true;
}
size_t parcelVertices(const std::vector<Vertex>& vertices){
    size_t count=0;for(const auto& vertex:vertices)if(same(vertex.color,{.51f,.32f,.16f}))++count;return count;
}
void stationaryAndWalking(){
    for(auto activity:{PedestrianActivity::Walk,PedestrianActivity::Wait,PedestrianActivity::Work,PedestrianActivity::Talk}){
        Game game=actor(activity);auto& person=game.pedestrians.back();
        person.phase=0;const auto planted=nearby(game,{},.70f);
        person.phase=1.57f;assert(sameGeometry(planted,nearby(game,{},.70f)));
    }
    Game game=actor(PedestrianActivity::Walk);auto& person=game.pedestrians.back();person.motion=.6f;
    person.phase=0;const auto first=nearby(game,{},.70f);person.phase=1.57f;
    assert(!sameGeometry(first,nearby(game,{},.70f)));
}
void gesturesAndOwnership(){
    Game game=actor(PedestrianActivity::Talk);auto& person=game.pedestrians.back();
    const auto talking=nearby(game,{}),feet=nearby(game,{},.70f);
    person.activityTime=.65f;assert(!sameGeometry(talking,nearby(game,{})));
    assert(sameGeometry(feet,nearby(game,{},.70f)));
    person.activity=PedestrianActivity::Work;person.activityTime=0;
    const auto working=nearby(game,{});assert(!sameGeometry(working,talking));
    person.activity=PedestrianActivity::Carry;assert(parcelVertices(nearby(game,{}))==0);
    person.carrying=true;
    for(float observerDistance:{10.f,60.f}){
        game.player={observerDistance,0,0};
        for(auto activity:{PedestrianActivity::Walk,PedestrianActivity::Wait,PedestrianActivity::Carry,PedestrianActivity::Startle,PedestrianActivity::Flee}){
            person.activity=activity;assert(parcelVertices(nearby(game,{}))>0);
        }
    }
    game.player={10,0,0};
    person.carrying=false;person.activity=PedestrianActivity::Startle;person.activityTime=0;
    const auto alarmed=nearby(game,{});size_t raised=0;
    for(const auto& vertex:alarmed)if(std::abs(vertex.position.x)>.15f&&vertex.position.y>1.45f&&vertex.position.z>.16f)++raised;
    assert(raised>0);
    person.activityTime=1;assert(!sameGeometry(alarmed,nearby(game,{})));
    person.activity=PedestrianActivity::Flee;person.motion=1;assert(!sameGeometry(talking,nearby(game,{})));
}
void stableAppearance(){
    Game game=actor(PedestrianActivity::Wait);const auto before=nearby(game,{});
    const Pedestrian person=game.pedestrians.back();game.pedestrians.back().position={1000,0,1000};game.pedestrians.push_back(person);
    assert(sameGeometry(before,nearby(game,{})));
}
void authoredSeatedContact(){
    size_t seats=0;
    for(const auto& place:World::pedestrianPlaces())if(place.kind==PedestrianPlaceKind::Seat){
      for(float observerDistance:{10.f,60.f}){
        Game game=actor(PedestrianActivity::Sit);auto& person=game.pedestrians.back();
        person.position=place.position;person.yaw=place.yaw;person.seatPosition=place.seatPosition;person.sitBlend=1;
        game.player=place.position+right(place.yaw)*observerDistance;
        const auto vertices=nearby(game,place.seatPosition);
        float minimumY=std::numeric_limits<float>::infinity(),maximumY=-minimumY;
        size_t supported=0,feet=0;
        for(const auto& vertex:vertices){
            minimumY=std::min(minimumY,vertex.position.y);maximumY=std::max(maximumY,vertex.position.y);
            const Vec3 offset=vertex.position-place.seatPosition;
            const float across=dot(offset,right(place.yaw)),along=dot(offset,forward(place.yaw));
            if(same(vertex.color,{.055f,.065f,.081f})&&std::abs(across)<.25f&&std::abs(along)<.40f){
                assert(vertex.position.y>=place.seatPosition.y-.12f);++supported;
            }
            if(vertex.position.y<place.position.y+.15f){assert(along>.40f);++feet;}
        }
        assert(supported>20&&feet>20);
        assert(minimumY>=place.position.y-.02f&&minimumY<place.position.y+.02f);
        assert(maximumY>place.seatPosition.y+.90f&&maximumY<place.seatPosition.y+1.08f);
        for(float blend:{0.f,.25f,.5f,.75f}){person.sitBlend=blend;nearby(game,place.position);}
        person.activity=PedestrianActivity::Startle;person.activityTime=.1f;person.sitBlend=.5f;nearby(game,place.position);
        person.health=0;person.sitBlend=1;nearby(game,place.position);
      }
        ++seats;
    }
    assert(seats>=8);std::printf("Seated contact: %zu authored pelvis anchors at both detail levels, grounded feet and clear thighs passed.\n",seats);
}
void invalidTransientPose(){
    Game game=actor(PedestrianActivity::Sit);auto& person=game.pedestrians.back();
    person.sitBlend=1;person.seatPosition={std::numeric_limits<float>::quiet_NaN(),0,0};
    person.motion=std::numeric_limits<float>::infinity();person.activityTime=std::numeric_limits<float>::quiet_NaN();
    nearby(game,{});
}
}
int main(){
    stationaryAndWalking();gesturesAndOwnership();stableAppearance();authoredSeatedContact();invalidTransientPose();
    std::puts("Pedestrian visuals: stationary feet, displacement-driven stride, conversation/work/startle/run gestures, real parcel ownership, stable identity, pose blending and finite normals passed.");
}
