#pragma once
#include "world.h"
#include <array>
#include <cfloat>

namespace mc {
// Conservative D3D clip half-spaces for row-vector matrices. XY-only volumes
// match the shadow rasterizer's disabled depth clipping: distant Z casters
// must remain eligible. Coefficients are derived from the exact float matrix;
// double support tests plus a float-dot roundoff bound avoid false culls.
struct ClipPlane {std::array<double,4> coefficient{},roundoff{};double padding=0;bool valid=false;};
struct ClipVolume {
    std::array<ClipPlane,6> planes{};unsigned count=6;
    explicit ClipVolume(const Mat4& matrix,bool shadowXY=false):count(shadowXY?4:6){
        const unsigned column[6]={0,0,1,1,2,2};const double sign[6]={1,-1,1,-1,1,-1};
        for(unsigned i=0;i<count;++i){
            auto& p=planes[i];bool finite=true;
            for(unsigned row=0;row<4;++row){
                const double component=matrix.m[row*4+column[i]],w=i==4?0:matrix.m[row*4+3];
                p.coefficient[row]=w+sign[i]*component;
                p.roundoff[row]=8*FLT_EPSILON*(std::abs(w)+std::abs(component));
                finite=finite&&std::isfinite(p.coefficient[row])&&std::isfinite(p.roundoff[row]);
            }
            double length=std::sqrt(p.coefficient[0]*p.coefficient[0]+p.coefficient[1]*p.coefficient[1]+p.coefficient[2]*p.coefficient[2]);
            p.valid=finite&&length>1e-15;p.padding=.25*length;
        }
    }
    bool visible(const Box& b)const{
        const float low[3]={b.min.x,b.min.y,b.min.z},high[3]={b.max.x,b.max.y,b.max.z};
        for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(low[axis])||!std::isfinite(high[axis])||low[axis]>high[axis])return true;
        for(unsigned i=0;i<count;++i){const auto& p=planes[i];if(!p.valid)continue;
            double support=p.coefficient[3],epsilon=p.padding+p.roundoff[3];
            for(unsigned axis=0;axis<3;++axis){support+=p.coefficient[axis]*(p.coefficient[axis]>=0?high[axis]:low[axis]);epsilon+=p.roundoff[axis]*std::max(std::abs(low[axis]),std::abs(high[axis]));}
            if(support < -epsilon)return false;
        }
        return true;
    }
};

inline Mat4 reversePerspective(float fov,float aspect,float nearPlane,float farPlane) {
    const float y=1/std::tan(fov*.5f),x=y/aspect,q=nearPlane/(nearPlane-farPlane);
    return {{x,0,0,0,0,y,0,0,0,0,q,1,0,0,-farPlane*q,0}};
}
struct VisibilityRange {float start=260,end=440;bool horizontal=false;};
inline VisibilityRange visibilityRange(float coverageRadius) {
    if(!std::isfinite(coverageRadius)||coverageRadius<0)return {};
    const float end=std::min(2000.0f,std::max(0.0f,coverageRadius-16.0f));
    return {std::max(0.0f,end-std::min(300.0f,end*.35f)),end,true};
}
inline bool meshBounds(const Mesh& mesh,Box& result) {
    result={};if(mesh.vertices.empty())return true;
    result.min=result.max=mesh.vertices.front().position;
    for(const auto& vertex:mesh.vertices){
        const Vec3 p=vertex.position;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
        result.min={std::min(result.min.x,p.x),std::min(result.min.y,p.y),std::min(result.min.z,p.z)};
        result.max={std::max(result.max.x,p.x),std::max(result.max.y,p.y),std::max(result.max.z,p.z)};
    }
    return true;
}
}
