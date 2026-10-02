#include "../src/law_navigation.h"
#include "../src/world.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
mc::World emptyWorld(){
    mc::World world;
    for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x){mc::Chunk chunk;chunk.x=x;chunk.z=z;world.chunks.push_back(std::move(chunk));}
    return world;
}
void addWall(mc::World& world,mc::Vec3 minimum,mc::Vec3 maximum){world.chunks[24].solids.push_back({minimum,maximum});}
void complete(mc::LawNavigation& navigation,const mc::LawSpace& space){
    for(int i=0;i<2000&&navigation.status()==mc::LawPathStatus::Building;++i)navigation.step(space,7);
    require(navigation.status()!=mc::LawPathStatus::Building,"bounded local route did not terminate");
    require(navigation.stats().nodes<=mc::LawNavigation::MaxNodes,"navigation node capacity exceeded");
    require(navigation.path().count<=mc::LawPath::Capacity,"navigation path capacity exceeded");
}
void checkPath(const mc::LawNavigation& navigation,const mc::LawSpace& space,mc::Vec3 from,mc::Vec3 target,float radius){
    require(navigation.status()==mc::LawPathStatus::Ready,"reachable destination has no ready route");
    const auto& path=navigation.path();require(path.count>0,"ready route is empty");
    for(uint32_t i=0;i<path.count;++i){const auto point=path.points[i];require(std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z),"route has a non-finite point");require(space.ready(point)&&space.walkClear(from,point,radius),"route crosses unavailable or blocking geometry");from=point;}
    require(mc::length(from-target)<.05f,"route failed to reach its requested destination");
}
void availabilityAndProjection(){
    auto world=emptyWorld();mc::WorldLawSpace space(world);
    require(space.ready({0,0,0}),"coordinate-correct collision fixture is not ready");
    mc::Vec3 ground;require(space.project({0,5,0},.35f,ground)&&std::abs(ground.y-world.height(0,0))<.001f,"projection did not resolve real ground height");
    addWall(world,{-1,-1,-1},{1,3,1});require(!space.project({0,0,0},.35f,ground),"projection accepted a standing position inside a wall");
    world.chunks.erase(world.chunks.begin()+24);
    require(!space.ready({0,0,0})&&!space.project({5,0,0},.35f,ground),"missing collision was accepted as known clear space");
    require(!space.lineClear({-5,1,0},{5,1,0})&&!space.walkClear({-5,0,0},{5,0,0},.35f),"queries passed through unavailable collision");
}
void lineAndBodyClearance(){
    auto world=emptyWorld();mc::WorldLawSpace space(world);
    addWall(world,{-2,0,4},{2,1.25f,6});
    require(space.lineClear({0,1.6f,0},{0,1.1f,10}),"clear eye ray was blocked by lower cover");
    require(!space.lineClear({0,1,0},{0,1.1f,10}),"lower muzzle ray crossed cover");
    require(!space.walkClear({0,0,0},{0,0,10},.35f),"standing body crossed lower cover");
    world.chunks[24].solids.clear();addWall(world,{-2,1.67f,4},{2,1.8f,6});
    require(space.lineClear({0,1.55f,0},{0,1.55f,10}),"eye ray below a beam was blocked");
    require(!space.walkClear({0,0,0},{0,0,10},.35f),"navigation ignored middle-of-segment standing headroom");
    world.chunks[24].solids.clear();addWall(world,{0,0,-5},{.02f,3,5});
    require(!space.lineClear({-20,1,0},{20,1,0}),"thin wall did not block an axis-aligned ray");
    require(!space.walkClear({-20,0,0},{20,0,0},.35f),"long navigation segment tunneled through a thin wall");
}
void terrainOccludesSight(){
    mc::World world;mc::Vec3 a,b;bool found=false;
    for(int x=3900;x<=4300&&!found;x+=50)for(int z=-500;z<=0&&!found;z+=50){
        mc::Vec3 left{float(x)-160,0,float(z)},right{float(x)+160,0,float(z)};
        left.y=world.height(left.x,left.z)+1.6f;right.y=world.height(right.x,right.z)+1.6f;
        for(int step=1;step<160;++step){const auto p=mc::lerp(left,right,float(step)/160);if(world.height(p.x,p.z)>p.y+.2f){a=left;b=right;found=true;break;}}
    }
    require(found,"terrain fixture did not find a natural ridge between eye-height endpoints");
    const auto center=(a+b)*.5f;const int cx=int(std::floor(center.x/128)),cz=int(std::floor(center.z/128));
    for(int z=cz-3;z<=cz+3;++z)for(int x=cx-3;x<=cx+3;++x){mc::Chunk chunk;chunk.x=x;chunk.z=z;world.chunks.push_back(std::move(chunk));}
    mc::WorldLawSpace space(world);require(space.ready(a)&&space.ready(b),"terrain fixture endpoints are unavailable");
    require(!space.lineClear(a,b),"bare terrain ridge did not occlude sight");
    a.y+=200;b.y+=200;require(space.lineClear(a,b),"ray above the terrain ridge was blocked");
}
void clearAndDetourRoutes(){
    auto world=emptyWorld();mc::WorldLawSpace space(world);mc::LawNavigation navigation;
    const mc::Vec3 from{-12,0,0},to{12,0,0};
    require(navigation.begin(space,from,to),"clear route did not begin");complete(navigation,space);checkPath(navigation,space,from,to,.35f);
    addWall(world,{-1,-1,-6},{1,3,6});
    require(navigation.begin(space,from,to),"detour route did not begin");complete(navigation,space);checkPath(navigation,space,from,to,.35f);
    bool around=false;for(uint32_t i=0;i<navigation.path().count;++i)around|=std::abs(navigation.path().points[i].z)>6;
    require(around,"detour did not go around the wall end");
}
void cornersAndNarrowOpening(){
    auto world=emptyWorld();mc::WorldLawSpace space(world);mc::LawNavigation navigation;
    addWall(world,{-1,-1,-7},{1,3,3});addWall(world,{-1,-1,3},{8,3,5});
    const mc::Vec3 from{-8,0,0},to{10,0,0};
    require(navigation.begin(space,from,to),"corner route did not begin");complete(navigation,space);checkPath(navigation,space,from,to,.35f);
    world.chunks[24].solids.clear();addWall(world,{-10,-1,3},{-.3f,3,5});addWall(world,{.3f,-1,3},{10,3,5});
    require(space.lineClear({0,1,0},{0,1,8}),"narrow-opening sight fixture is not clear");
    require(!space.walkClear({0,0,0},{0,0,8},.35f),"route accepted an opening narrower than the officer body");
}
void enclosedDestinationAndReset(){
    auto world=emptyWorld();mc::WorldLawSpace space(world);mc::LawNavigation navigation;
    addWall(world,{-4,-1,-4},{-3,3,4});addWall(world,{3,-1,-4},{4,3,4});
    addWall(world,{-4,-1,-4},{4,3,-3});addWall(world,{-4,-1,3},{4,3,4});
    navigation.begin(space,{-12,0,0},{0,0,0});complete(navigation,space);
    require(navigation.status()==mc::LawPathStatus::Failed,"enclosed destination produced a false route");
    navigation.clear();require(navigation.status()==mc::LawPathStatus::Idle&&navigation.path().count==0,"clear retained stale route data");
    world.chunks[24].solids.clear();require(navigation.begin(space,{-12,0,0},{0,0,0}),"navigator failed to restart after an unreachable route");complete(navigation,space);checkPath(navigation,space,{-12,0,0},{0,0,0},.35f);
}
class CountingSpace final:public mc::LawSpace {
public:
    explicit CountingSpace(const mc::LawSpace& source):source_(source){}
    mutable uint32_t checks=0,projects=0;
    bool ready(mc::Vec3 p)const override{return source_.ready(p);}
    bool project(mc::Vec3 p,float r,mc::Vec3& out)const override{++checks;++projects;return source_.project(p,r,out);}
    bool walkClear(mc::Vec3 a,mc::Vec3 b,float r)const override{++checks;return source_.walkClear(a,b,r);}
    bool lineClear(mc::Vec3 a,mc::Vec3 b)const override{++checks;return source_.lineClear(a,b);}
    size_t obstacles(mc::Vec3 p,float r,std::span<mc::LawObstacle> out)const override{return source_.obstacles(p,r,out);}
private:
    const mc::LawSpace& source_;
};
void incrementalWorkBounds(){
    auto world=emptyWorld();addWall(world,{-1,-1,-6},{1,3,6});mc::WorldLawSpace geometry(world);
    CountingSpace space(geometry);mc::LawNavigation navigation;
    require(navigation.begin(space,{-12,0,0},{12,0,0}),"budgeted route did not begin");
    require(space.projects<=5,"navigation setup exceeded bounded projection work");
    for(int step=0;step<5000&&navigation.status()==mc::LawPathStatus::Building;++step){
        space.checks=0;navigation.step(space,3);
        require(space.checks<=3,"navigation exceeded actual per-step geometry query budget");
        require(navigation.stats().nodes<=mc::LawNavigation::MaxNodes,"incremental route exceeded node storage");
    }
    checkPath(navigation,geometry,{-12,0,0},{12,0,0},.35f);
}
void obstacleBoundsAndInvalidInput(){
    auto world=emptyWorld();mc::WorldLawSpace space(world);
    for(int i=0;i<10;++i)addWall(world,{float(i)*3,0,10},{float(i)*3+1,3,11});
    std::array<mc::LawObstacle,3> obstacles{};
    require(space.obstacles({0,0,0},50,obstacles)<=obstacles.size(),"obstacle query wrote beyond requested capacity");
    std::span<mc::LawObstacle> empty;require(space.obstacles({0,0,0},50,empty)==0,"empty obstacle output accepted entries");
    mc::LawNavigation navigation;
    require(!navigation.begin(space,{std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,0}),"navigation accepted NaN origin");
    require(!navigation.begin(space,{0,0,0},{5,0,0},-1),"navigation accepted negative body radius");
}
}
int main(){
    struct Test{const char* name;void(*run)();};
    const Test tests[]={{"availability and ground projection",availabilityAndProjection},{"sight and standing-body clearance",lineAndBodyClearance},{"terrain occludes sight",terrainOccludesSight},{"clear and wall-detour routes",clearAndDetourRoutes},{"corners and narrow opening",cornersAndNarrowOpening},{"unreachable destination and reset",enclosedDestinationAndReset},{"incremental geometry work bounds",incrementalWorkBounds},{"obstacle bounds and invalid input",obstacleBoundsAndInvalidInput}};
    int failures=0;for(const auto& test:tests){try{test.run();std::cout<<"PASS "<<test.name<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<'\n';}}
    std::cout<<std::size(tests)-failures<<'/'<<std::size(tests)<<" law navigation tests passed\n";return failures?1:0;
}
