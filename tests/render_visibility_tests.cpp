#include "../src/render_visibility.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
using namespace mc;
std::array<double,6> cornerHalfspaces(const Mat4& m,Vec3 point){
    // Float projection independently models the shader's row-vector dot products.
    const float input[4]={point.x,point.y,point.z,1};float clip[4]{};
    for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)clip[col]+=input[row]*m.m[row*4+col];
    return {double(clip[3])+clip[0],double(clip[3])-clip[0],double(clip[3])+clip[1],double(clip[3])-clip[1],clip[2],double(clip[3])-clip[2]};
}
bool cornerOracle(const Mat4& m,const Box& b,unsigned count){
    std::array<double,6> maxima;maxima.fill(-std::numeric_limits<double>::infinity());
    for(unsigned corner=0;corner<8;++corner){auto h=cornerHalfspaces(m,{corner&1?b.max.x:b.min.x,corner&2?b.max.y:b.min.y,corner&4?b.max.z:b.min.z});for(unsigned i=0;i<count;++i)maxima[i]=std::max(maxima[i],h[i]);}
    for(unsigned i=0;i<count;++i)if(maxima[i]<0)return false;
    return true;
}
void verifySupports(const ClipVolume& f,const Box& b){
    const double low[3]={b.min.x,b.min.y,b.min.z},high[3]={b.max.x,b.max.y,b.max.z};
    for(unsigned i=0;i<f.count;++i){const auto& p=f.planes[i];double support=p.coefficient[3],corners=-std::numeric_limits<double>::infinity();
        for(unsigned axis=0;axis<3;++axis)support+=p.coefficient[axis]*(p.coefficient[axis]>=0?high[axis]:low[axis]);
        for(unsigned corner=0;corner<8;++corner){double value=p.coefficient[3];for(unsigned axis=0;axis<3;++axis)value+=p.coefficient[axis]*(corner&(1u<<axis)?high[axis]:low[axis]);corners=std::max(corners,value);}
        assert(std::abs(support-corners)<=1e-8*(1+std::abs(corners)));
    }
}
int main(){
    auto legacy=visibilityRange(-1);assert(!legacy.horizontal&&legacy.start==260&&legacy.end==440);
    auto empty=visibilityRange(0);assert(empty.horizontal&&empty.start==0&&empty.end==0);
    auto close=visibilityRange(384);assert(close.horizontal&&close.end==368&&close.start<close.end);
    auto distant=visibilityRange(2112);assert(distant.horizontal&&distant.end==2000&&distant.start==1700);
    Mesh mesh;mesh.vertices={{{-4,2,3},{0,1,0},{1,1,1},0},{{6,-2,-1},{0,1,0},{1,1,1},0}};
    Box bounds;assert(meshBounds(mesh,bounds));assert(bounds.min.x==-4&&bounds.min.y==-2&&bounds.min.z==-1&&bounds.max.x==6&&bounds.max.y==2&&bounds.max.z==3);
    mesh.vertices[1].position.x=std::numeric_limits<float>::infinity();assert(!meshBounds(mesh,bounds));

    Mat4 view=lookAt({0,0,0},{0,0,1}),projection=perspective(68*Pi/180,16.f/9,.12f,900);Mat4 vp=multiply(view,projection);ClipVolume main(vp);
    assert(main.visible({{-1,-1,10},{1,1,11}}));assert(!main.visible({{-1,-1,-20},{1,1,-19}}));
    assert(main.visible({{-100,-100,-100},{100,100,100}})); // Camera inside.
    assert(main.visible({{0,0,.1199f},{0,0,.1201f}})); // Straddling near plane.
    const float edge=50*std::tan(34*Pi/180)*(16.f/9);
    assert(main.visible({{edge,0,50},{edge,0,50}})); // Zero-volume, edge-on box.
    assert(!main.visible({{edge+20,0,50},{edge+20,0,50}}));
    assert(!main.visible({{0,0,950},{0,0,951}}));
    Mat4 reversed=reversePerspective(68*Pi/180,16.f/9,.12f,3000.f);
    auto depth=[&](float z){return (z*reversed.m[10]+reversed.m[14])/(z*reversed.m[11]+reversed.m[15]);};
    assert(std::abs(depth(.12f)-1)<1e-6f);assert(std::abs(depth(3000))<1e-7f);
    float previous=depth(.12f);for(int sample=1;sample<=10000;++sample){float z=.12f*std::pow(3000.f/.12f,float(sample)/10000);float current=depth(z);assert(current<=previous);previous=current;}
    assert(ClipVolume(reversed).visible({{-1,-1,10},{1,1,11}}));assert(!ClipVolume(reversed).visible({{-1,-1,-20},{1,1,-19}}));
    Mat4 identity{};identity.m[0]=identity.m[5]=identity.m[10]=identity.m[15]=1;
    Box tall{{-.2f,-.2f,10000},{.2f,.2f,11000}};
    assert(ClipVolume(identity,true).visible(tall));assert(!ClipVolume(identity).visible(tall));
    Box bad{{std::numeric_limits<float>::quiet_NaN(),0,0},{1,1,1}};assert(main.visible(bad));
    Mat4 invalid{};invalid.m[0]=std::numeric_limits<float>::quiet_NaN();assert(ClipVolume(invalid).visible({{9000,9000,9000},{9001,9001,9001}}));
    std::mt19937 generator(925771);auto random=[&](float low,float high){return std::uniform_real_distribution<float>(low,high)(generator);};
    unsigned visibleOracle=0,conservativeOnly=0;
    constexpr unsigned cases=200000;
    for(unsigned i=0;i<cases;++i){
        Vec3 eye{random(-6144,6144),random(0,1150),random(-6144,6144)};
        float yaw=random(-Pi,Pi),pitch=random(-1.3f,1.3f);Vec3 forward{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        Vec3 right=normalized(cross({0,1,0},forward)),up=cross(forward,right);
        float distance=random(.01f,3200),x=random(-1.15f,1.15f),y=random(-1.15f,1.15f);
        Vec3 center=eye+forward*distance+right*(x*distance*1.2f)+up*(y*distance*.7f),half{random(0,150),random(0,180),random(0,150)};
        Box b{center-half,center+half};float farClip=(i&1)?900.f:3000.f;
        Mat4 randomProjection=(i&2)?reversePerspective(68*Pi/180,16.f/9,.12f,farClip):perspective(68*Pi/180,16.f/9,.12f,farClip);
        Mat4 m=multiply(lookAt(eye,eye+forward),randomProjection);
        ClipVolume f(m);verifySupports(f,b);bool oracle=cornerOracle(m,b,6),visible=f.visible(b);assert(!oracle||visible);
        visibleOracle+=oracle;conservativeOnly+=visible&&!oracle;
        Mat4 light{};light.m[0]=light.m[5]=2.f/240;light.m[10]=1.f/900;light.m[15]=1;
        m=multiply(lookAt(eye+Vec3{120,450,80},eye),light);ClipVolume shadow(m,true);verifySupports(shadow,b);
        assert(!cornerOracle(m,b,4)||shadow.visible(b));
    }
    std::printf("Culling math: %u perspective (half reverse-Z) + %u shadow cases passed; 8-corner supports matched; no false culls against float clip oracle.\n",cases,cases);
    std::printf("Perspective oracle-visible=%u; extra conservative draws=%u (padding/roundoff).\n",visibleOracle,conservativeOnly);
    std::puts("Explicit cases passed: camera inside, near-plane intersection, edge-on zero-volume box, outside side/far planes, shadow depth-disabled caster, invalid input fallback, reverse-Z near=1/far=0 and monotonic depth.");
}
