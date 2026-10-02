#include "../src/world.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace mc;
namespace {
bool close(float a,float b,float tolerance=.005f){return std::abs(a-b)<=tolerance;}
const Chunk& find(const World& w,int x,int z){for(const auto& c:w.chunks)if(c.x==x&&c.z==z)return c;assert(false);return w.chunks.front();}
void validateMesh(const Mesh& m) {
    assert(!m.vertices.empty());assert(m.indices.size()%3==0);
    for(uint32_t i:m.indices)assert(i<m.vertices.size());
    for(const auto& v:m.vertices) {
        assert(std::isfinite(v.position.x)&&std::isfinite(v.position.y)&&std::isfinite(v.position.z));
        assert(std::isfinite(v.normal.x)&&std::isfinite(v.normal.y)&&std::isfinite(v.normal.z));
        assert(close(length(v.normal),1,.001f));assert(v.material>=0&&v.material<=4);
    }
}
void geometry() {
    Mesh m;addBox(m,{0,0,0},{1,2,3},{1,1,1});assert(m.vertices.size()==24);assert(m.indices.size()==36);validateMesh(m);
    for(const auto& v:m.vertices)assert(dot(v.position,v.normal)>0);
    Mesh c;addCylinder(c,{0,0,0},2,4,{1,1,1},8);validateMesh(c);
    for(const auto& v:c.vertices)assert(dot(v.position-Vec3{0,2,0},v.normal)>0);
    size_t old=m.vertices.size();appendMesh(m,c);assert(m.vertices.size()==old+c.vertices.size());validateMesh(m);
}
bool sameVector(Vec3 a,Vec3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool sourceOnEmissiveFace(const Mesh& m,Vec3 p) {
    for(size_t i=0;i<m.indices.size();i+=3) {
        const Vertex& a=m.vertices[m.indices[i]];const Vertex& b=m.vertices[m.indices[i+1]];const Vertex& c=m.vertices[m.indices[i+2]];
        if(a.material!=2 || b.material!=2 || c.material!=2 || a.normal.y>-.999f)continue;
        if(!close(a.position.y,p.y,.001f) || !close(b.position.y,p.y,.001f) || !close(c.position.y,p.y,.001f))continue;
        auto side=[](Vec3 p0,Vec3 p1,Vec3 point){return (p1.x-p0.x)*(point.z-p0.z)-(p1.z-p0.z)*(point.x-p0.x);};
        float ab=side(a.position,b.position,p),bc=side(b.position,c.position,p),ca=side(c.position,a.position,p);
        if((ab>=-.001f&&bc>=-.001f&&ca>=-.001f)||(ab<=.001f&&bc<=.001f&&ca<=.001f))return true;
    }
    return false;
}
void lighting() {
    World w;w.stream({8,0,8});const Chunk& origin=find(w,0,0);
    assert(origin.lights.size()==15); // Twelve street lamps and three market downlights.
    for(float along:{24.0f,64.0f,104.0f})for(Vec3 expected:{Vec3{9.2f,8.31f,along},Vec3{118.8f,8.31f,along},Vec3{along,8.31f,9.2f},Vec3{along,8.31f,118.8f}}) {
        int count=0;
        for(const auto& light:origin.lights)if(length(light.position-expected)<.001f) {
            ++count;assert(light.radius==28&&light.intensity==100);assert(sameVector(light.color,{1,.72f,.40f}));
            assert(sameVector(light.direction,{0,-1,0})&&light.cone==-.15f);
        }
        assert(count==1);
    }
    for(float z:{35.0f,54.0f,73.0f}) {
        int count=0;for(const auto& light:origin.lights)if(length(light.position-Vec3{88.3f,3.07f,z})<.001f) {
            ++count;assert(light.radius==11&&light.intensity==22);assert(sameVector(light.color,{1,.78f,.46f}));
        }
        assert(count==1);
    }
    size_t lamps=0,shops=0;
    for(const auto& chunk:w.chunks)for(const auto& light:chunk.lights) {
        assert(std::isfinite(light.position.x)&&std::isfinite(light.position.y)&&std::isfinite(light.position.z));
        assert(std::isfinite(light.radius)&&light.radius>0&&light.radius<=64);
        assert(std::isfinite(light.intensity)&&light.intensity>0&&light.intensity<=256);
        assert(std::isfinite(light.cone)&&light.cone>=-1&&light.cone<1);
        assert(light.color.x>=0&&light.color.x<=1&&light.color.y>=0&&light.color.y<=1&&light.color.z>=0&&light.color.z<=1);
        assert(close(length(light.direction),1,.001f));assert(sourceOnEmissiveFace(chunk.mesh,light.position));
        if(light.radius==28)++lamps;else ++shops;
    }
    assert(lamps==588);assert(shops>0&&shops<300);
    assert(w.blocked({11,0,24},.2f));assert(!w.blocked({12,0,24},.45f));
    std::printf("Central light sources: %zu street lamps, %zu shop/market downlights\n",lamps,shops);
}
void streamingAndSeams() {
    World w;assert(w.stream({8,0,8}));assert(w.revision==1);assert(w.chunks.size()==49);
    assert(!w.stream({127.99f,0,127.99f}));assert(w.revision==1);
    const Chunk snapshot=find(w,0,0);const Vertex* data=find(w,0,0).mesh.vertices.data();const Light* lightData=find(w,0,0).lights.data();
    Mesh combined=w.combinedMesh();validateMesh(combined);
    std::printf("Central 49 chunks: %zu vertices, %zu triangles, %zu collision solids in block 0,0\n",combined.vertices.size(),combined.indices.size()/3,snapshot.solids.size());
    assert(combined.indices.size()/3<350000);
    assert(w.stream({128,0,0}));assert(w.revision==2);assert(w.chunks.size()==49);
    assert(find(w,0,0).lights.data()==lightData);
    assert(find(w,0,0).mesh.vertices.data()==data); // Retained chunks keep their allocated geometry.
    assert(w.stream({-128,0,-128}));assert(w.chunks.size()==49);
    const auto& a=find(w,0,0);const auto& b=find(w,1,0);
    for(int i=0;i<=16;++i) {
        float z=i*8.0f;bool left=false,right=false;float h=w.height(128,z);
        for(const auto& v:a.mesh.vertices)if(close(v.position.x,128)&&close(v.position.z,z)&&close(v.position.y,h))left=true;
        for(const auto& v:b.mesh.vertices)if(close(v.position.x,128)&&close(v.position.z,z)&&close(v.position.y,h))right=true;
        assert(left&&right);
    }
    assert(w.stream({4096,0,2048}));assert(w.stream({8,0,8}));
    const auto& restored=find(w,0,0);
    assert(restored.mesh.vertices.size()==snapshot.mesh.vertices.size());assert(restored.mesh.indices==snapshot.mesh.indices);
    assert(restored.lights.size()==snapshot.lights.size());
    for(size_t i=0;i<snapshot.lights.size();++i) {
        const Light& originalLight=snapshot.lights[i];const Light& restoredLight=restored.lights[i];
        assert(sameVector(originalLight.position,restoredLight.position)&&sameVector(originalLight.direction,restoredLight.direction));
        assert(sameVector(originalLight.color,restoredLight.color)&&originalLight.radius==restoredLight.radius);
        assert(originalLight.intensity==restoredLight.intensity&&originalLight.cone==restoredLight.cone);
    }
    for(size_t i=0;i<snapshot.mesh.vertices.size();++i) {
        const auto& originalVertex=snapshot.mesh.vertices[i];const auto& restoredVertex=restored.mesh.vertices[i];
        assert(originalVertex.position.x==restoredVertex.position.x&&originalVertex.position.y==restoredVertex.position.y&&originalVertex.position.z==restoredVertex.position.z);
        assert(originalVertex.color.x==restoredVertex.color.x&&originalVertex.color.y==restoredVertex.color.y&&originalVertex.color.z==restoredVertex.color.z&&originalVertex.material==restoredVertex.material);
    }
}
void geography() {
    World w;assert(w.height(8,8)==0);assert(w.road(8,8));assert(!w.road(64,64));
    assert(w.biome(0,0)==Biome::Downtown);assert(w.biome(-2000,0)==Biome::Residential);
    assert(w.biome(-4200,1500)==Biome::Countryside);assert(w.biome(1100,-2800)==Biome::Wetland);
    assert(w.biome(4140,-250)==Biome::Island);assert(w.biome(5900,500)==Biome::Ocean);
    assert(w.road(3200,0));assert(close(w.height(3200,0),5.2f));assert(close(w.height(3200,10),5.2f));
    for(float z=-5000;z<5000;z+=187)for(float x=-5000;x<5000;x+=197) {
        float h=w.height(x,z);assert(std::isfinite(h));assert(h>-100&&h<300);
        // Natural terrain is continuous away from the elevated bridge's intentional edge.
        if(!(x>2200&&std::abs(z)<12))assert(std::abs(h-w.height(x+.01f,z))<.25f);
    }
    w.stream({8,0,8});assert(!w.blocked({8,0,8},.45f));
    // New street furniture and plaza landmarks preserve mission approach positions.
    for(Vec3 p:{Vec3{12,0,24},Vec3{268,0,128},Vec3{12,0,128}})assert(!w.blocked(p,.45f));
    assert(w.blocked({44,0,98},.45f));assert(w.blocked({96,0,35},.45f));
    for(Vec3 p: {Vec3{128,0,128},Vec3{-384,0,256},Vec3{768,0,-512},Vec3{-512,0,-768}})assert(w.road(p.x,p.z));
}
void collision() {
    World w;Chunk c;c.solids.push_back({{0,0,0},{2,5,10}});w.chunks.push_back(c);
    assert(w.blocked({1,0,5},.5f));assert(!w.blocked({-2,0,5},.5f));assert(!w.blocked({1,6,5},.5f));
    Vec3 p=w.move({-10,0,5},{1000,0,0},.5f);assert(p.x<-.5f&&p.x>-.51f);assert(!w.blocked(p,.5f));
    p=w.move({-10,0,1},{20,0,7},.5f);assert(p.x<-.5f);assert(close(p.z,8,.01f));
    p=w.move({-.5f,0,5},{10,0,0},.5f);assert(p.x<=-.5f); // Exact contact cannot tunnel inward.
    p=w.move({-10,6,5},{20,0,0},.5f);assert(close(p.x,10));
    p=w.move({1,0,5},{0,0,0},.5f);assert(!w.blocked(p,.5f));
    p=w.move({-10,0,-10},{0,3,0},.5f);assert(close(p.y,3));
    // A second perpendicular wall must stop the component left after the first wall slide.
    w.chunks.front().solids.push_back({{-10,0,9},{0,5,10}});
    p=w.move({-5,0,2},{20,0,20},.5f);assert(p.x<=-.5f&&p.z<=8.5f);assert(!w.blocked(p,.5f));
}
}
int main(){geometry();geography();lighting();streamingAndSeams();collision();std::puts("World tests passed.");}
