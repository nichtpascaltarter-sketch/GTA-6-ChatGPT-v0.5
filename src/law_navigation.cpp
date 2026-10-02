#include "law_navigation.h"
#include "world.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mc {
namespace {
bool finitePoint(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<=World::Extent&&std::abs(p.z)<=World::Extent&&std::abs(p.y)<=2048;}
bool segmentBox(Vec3 a,Vec3 b,Vec3 lo,Vec3 hi) {
    const Vec3 d=b-a;float enter=0,leave=1;
    const float origin[]{a.x,a.y,a.z},direction[]{d.x,d.y,d.z},minimum[]{lo.x,lo.y,lo.z},maximum[]{hi.x,hi.y,hi.z};
    for(int axis=0;axis<3;++axis) {
        if(std::abs(direction[axis])<1e-7f) {if(origin[axis]<=minimum[axis]||origin[axis]>=maximum[axis])return false;}
        else {
            float u=(minimum[axis]-origin[axis])/direction[axis],v=(maximum[axis]-origin[axis])/direction[axis];
            if(u>v)std::swap(u,v);
            enter=std::max(enter,u);leave=std::min(leave,v);
            if(enter>=leave)return false;
        }
    }
    return leave>0&&enter<1;
}
float planarSquared(Vec3 a,Vec3 b) {return (a.x-b.x)*(a.x-b.x)+(a.z-b.z)*(a.z-b.z);}
bool covered(const World& world,Vec3 a,Vec3 b) {
    if(!finitePoint(a)||!finitePoint(b))return false;
    const int steps=std::max(1,int(std::ceil(length(b-a)/16)));
    for(int step=0;step<=steps;++step)if(!world.collisionReady(lerp(a,b,float(step)/float(steps))))return false;
    return true;
}
}

bool WorldLawSpace::ready(Vec3 p)const {return finitePoint(p)&&world_.collisionReady(p);}
bool WorldLawSpace::project(Vec3 desired,float radius,Vec3& ground)const {
    if(!ready(desired)||!std::isfinite(radius)||radius<=0||radius>3)return false;
    ground={desired.x,world_.height(desired.x,desired.z),desired.z};
    if(ground.y<World::WaterLevel+.2f&&world_.waterDepth(ground.x,ground.z)>.7f)return false;
    return finitePoint(ground)&&!world_.blocked(ground,radius);
}
bool WorldLawSpace::lineClear(Vec3 from,Vec3 to)const {
    if(!covered(world_,from,to))return false;
    for(const auto& chunk:world_.chunks)for(const auto& box:chunk.solids)
        if(segmentBox(from,to,box.min,box.max))return false;
    const int steps=std::max(1,int(std::ceil(length(to-from)/2)));
    for(int step=0;step<=steps;++step) {
        const Vec3 p=lerp(from,to,float(step)/float(steps));
        if(p.y<world_.height(p.x,p.z)-.01f)return false;
    }
    return true;
}
bool WorldLawSpace::walkClear(Vec3 from,Vec3 to,float radius)const {
    if(!std::isfinite(radius)||radius<=0||radius>3||!covered(world_,from,to)||world_.blocked(from,radius)||world_.blocked(to,radius))return false;
    // A straight standing-capsule corridor. World::move deliberately slides, so it
    // cannot establish that a proposed graph edge actually follows this segment.
    for(const auto& chunk:world_.chunks)for(const auto& box:chunk.solids) {
        const Vec3 lo{box.min.x-radius,box.min.y-1.7f,box.min.z-radius};
        const Vec3 hi{box.max.x+radius,box.max.y,box.max.z+radius};
        if(segmentBox(from,to,lo,hi))return false;
    }
    const int steps=std::max(1,int(std::ceil(length(to-from)/2)));
    float previous=from.y;
    for(int step=0;step<=steps;++step) {
        const Vec3 point=lerp(from,to,float(step)/float(steps));Vec3 ground;
        if(!project(point,radius,ground)||std::abs(ground.y-point.y)>.65f||std::abs(ground.y-previous)>.65f)return false;
        previous=ground.y;
    }
    return true;
}
size_t WorldLawSpace::obstacles(Vec3 center,float radius,std::span<LawObstacle> output)const {
    if(!ready(center)||!std::isfinite(radius)||radius<=0||output.empty())return 0;
    size_t count=0;
    const auto distance=[center](const LawObstacle& b) {
        return planarSquared(center,{clamp(center.x,b.min.x,b.max.x),center.y,clamp(center.z,b.min.z,b.max.z)});
    };
    for(const auto& chunk:world_.chunks)for(const auto& box:chunk.solids) {
        if(center.y>=box.max.y||center.y+1.7f<=box.min.y)continue;
        LawObstacle candidate{box.min,box.max};const float d=distance(candidate);
        if(d>radius*radius)continue;
        size_t position=0;while(position<count&&distance(output[position])<=d)++position;
        if(position>=output.size())continue;
        const size_t end=std::min(count,output.size()-1);
        for(size_t i=end;i>position;--i)output[i]=output[i-1];
        output[position]=candidate;count=std::min(count+1,output.size());
    }
    return count;
}

void LawNavigation::clear() {
    status_=LawPathStatus::Idle;path_={};stats_={};obstacleCount_=corner_=nodeCount_=neighbor_=0;
    current_=-1;connected_=false;edges_.fill(0);settled_.fill(false);previous_.fill(-1);
    costs_.fill(std::numeric_limits<float>::infinity());destination_={};
}
bool LawNavigation::begin(const LawSpace& space,Vec3 from,Vec3 destination,float radius) {
    clear();status_=LawPathStatus::Failed;
    if(!finitePoint(from)||!finitePoint(destination)||!std::isfinite(radius)||radius<=0||radius>3)return false;
    radius_=radius;Vec3 start;
    if(!space.project(from,radius,start))return false;
    const Vec3 delta=destination-start;const float distance=length(delta);
    bool found=false;
    for(int attempt=0;attempt<4&&!found;++attempt) {
        const float reach=std::min(distance,64.0f-float(attempt)*16.0f);
        found=space.project(start+normalized(delta)*reach,radius,destination_);
        if(distance<=reach)break;
    }
    if(!found)return false;
    nodes_[0]=start;nodes_[1]=destination_;nodeCount_=2;costs_[0]=0;
    obstacleCount_=uint32_t(std::min(space.obstacles(lerp(start,destination_,.5f),64,obstacles_),MaxObstacles));
    status_=LawPathStatus::Building;return true;
}
void LawNavigation::connect() {
    // Sparse symmetric nearest-neighbor graph plus the direct goal edge.
    for(uint32_t node=0;node<nodeCount_;++node) {
        std::array<bool,MaxNodes> chosen{};chosen[node]=true;
        for(unsigned n=0;n<8&&n+1<nodeCount_;++n) {
            float best=std::numeric_limits<float>::infinity();uint32_t target=node;
            for(uint32_t other=0;other<nodeCount_;++other)if(!chosen[other]) {
                const float distance=planarSquared(nodes_[node],nodes_[other]);
                if(distance<best){best=distance;target=other;}
            }
            chosen[target]=true;edges_[node*MaxNodes+target]=edges_[target*MaxNodes+node]=1;
        }
    }
    edges_[1]=edges_[MaxNodes]=1;connected_=true;
}
void LawNavigation::finish(int node) {
    std::array<Vec3,LawPath::Capacity> reverse{};uint32_t count=0;
    while(node>=0) {
        if(count==reverse.size()){status_=LawPathStatus::Failed;return;}
        reverse[count++]=nodes_[size_t(node)];node=previous_[size_t(node)];
    }
    path_.count=count;
    for(uint32_t i=0;i<count;++i)path_.points[i]=reverse[count-i-1];
    status_=LawPathStatus::Ready;
}
void LawNavigation::step(const LawSpace& space,uint32_t queryBudget) {
    stats_={};stats_.nodes=nodeCount_;
    if(status_!=LawPathStatus::Building)return;
    queryBudget=std::min(queryBudget,64u);
    while(corner_<obstacleCount_*4&&stats_.checks<queryBudget) {
        const auto& obstacle=obstacles_[corner_/4];const uint32_t side=corner_%4;++corner_;
        const float margin=radius_+.12f;
        Vec3 desired{side&1?obstacle.max.x+margin:obstacle.min.x-margin,nodes_[0].y,side&2?obstacle.max.z+margin:obstacle.min.z-margin},ground;
        ++stats_.checks;
        if(planarSquared(desired,nodes_[0])>128*128||!space.project(desired,radius_,ground))continue;
        bool duplicate=false;for(uint32_t i=0;i<nodeCount_;++i)if(planarSquared(ground,nodes_[i])<.01f){duplicate=true;break;}
        if(!duplicate&&nodeCount_<MaxNodes)nodes_[nodeCount_++]=ground;
    }
    stats_.nodes=nodeCount_;
    if(corner_<obstacleCount_*4)return;
    if(!connected_)connect();
    while(stats_.expansions<16) {
        if(current_<0) {
            float best=std::numeric_limits<float>::infinity();
            for(uint32_t i=0;i<nodeCount_;++i)if(!settled_[i]&&costs_[i]<best){best=costs_[i];current_=int(i);}
            if(current_<0){status_=LawPathStatus::Failed;return;}
            if(current_==1){finish(current_);return;}
            neighbor_=0;
        }
        while(neighbor_<nodeCount_) {
            const uint32_t other=neighbor_;auto& edge=edges_[size_t(current_)*MaxNodes+other];
            if(edge==1) {
                if(stats_.checks>=queryBudget)return;
                ++stats_.checks;edge=space.walkClear(nodes_[size_t(current_)],nodes_[other],radius_)?2:3;
                edges_[size_t(other)*MaxNodes+size_t(current_)]=edge;
            }
            if(edge==2&&!settled_[other]) {
                const float candidate=costs_[size_t(current_)]+length(nodes_[other]-nodes_[size_t(current_)]);
                if(candidate<costs_[other]){costs_[other]=candidate;previous_[other]=current_;}
            }
            ++neighbor_;
        }
        settled_[size_t(current_)]=true;current_=-1;++stats_.expansions;
    }
}
}
