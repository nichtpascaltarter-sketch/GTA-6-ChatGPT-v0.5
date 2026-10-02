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
void streamingAndSeams() {
    World w;assert(w.stream({8,0,8}));assert(w.revision==1);assert(w.chunks.size()==49);
    assert(!w.stream({127.99f,0,127.99f}));assert(w.revision==1);
    const Chunk snapshot=find(w,0,0);const Vertex* data=find(w,0,0).mesh.vertices.data();
    Mesh combined=w.combinedMesh();validateMesh(combined);
    std::printf("Central 49 chunks: %zu vertices, %zu triangles, %zu collision solids in block 0,0\n",combined.vertices.size(),combined.indices.size()/3,snapshot.solids.size());
    assert(combined.indices.size()/3<300000);
    assert(w.stream({128,0,0}));assert(w.revision==2);assert(w.chunks.size()==49);
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
    for(size_t i=0;i<snapshot.mesh.vertices.size();++i) {
        const auto& a=snapshot.mesh.vertices[i];const auto& b=restored.mesh.vertices[i];
        assert(a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z);
        assert(a.color.x==b.color.x&&a.color.y==b.color.y&&a.color.z==b.color.z&&a.material==b.material);
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
int main(){geometry();geography();streamingAndSeams();collision();std::puts("World tests passed.");}
