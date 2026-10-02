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
        assert(std::isfinite(v.color.x)&&std::isfinite(v.color.y)&&std::isfinite(v.color.z));
        assert(std::isfinite(v.material));
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
void compareChunks(const Chunk& original,const Chunk& restored) {
    assert(original.x==restored.x&&original.z==restored.z);
    assert(original.mesh.vertices.size()==restored.mesh.vertices.size());
    assert(original.mesh.indices==restored.mesh.indices);
    for(size_t i=0;i<original.mesh.vertices.size();++i) {
        const auto& a=original.mesh.vertices[i];const auto& b=restored.mesh.vertices[i];
        assert(sameVector(a.position,b.position)&&sameVector(a.normal,b.normal));
        assert(sameVector(a.color,b.color)&&a.material==b.material);
    }
    assert(original.solids.size()==restored.solids.size());
    for(size_t i=0;i<original.solids.size();++i) {
        assert(sameVector(original.solids[i].min,restored.solids[i].min));
        assert(sameVector(original.solids[i].max,restored.solids[i].max));
    }
    assert(original.lights.size()==restored.lights.size());
    for(size_t i=0;i<original.lights.size();++i) {
        const auto& a=original.lights[i];const auto& b=restored.lights[i];
        assert(sameVector(a.position,b.position)&&sameVector(a.direction,b.direction));
        assert(sameVector(a.color,b.color)&&a.radius==b.radius&&a.intensity==b.intensity&&a.cone==b.cone);
    }
}
bool insideRoadCorridor(const World& w,float x,float z) {
    // A vehicle with a one-metre radius must fit comfortably inside the road.
    for(float dx:{-2.0f,0.0f,2.0f})for(float dz:{-2.0f,0.0f,2.0f})
        if(!w.road(x+dx,z+dz))return false;
    return true;
}
void naturalRegions() {
    struct Region {const char* name;Vec3 position;Biome biome;};
    const Region regions[]={
        {"Countryside",{-4096,0,2048},Biome::Countryside},
        {"Wetlands",{1100,0,-2800},Biome::Wetland},
        {"Island",{4140,0,-250},Biome::Island},
        {"Coast",{2520,0,768},Biome::Beach}
    };
    for(const auto& region:regions) {
        World w;assert(w.stream(region.position));assert(w.chunks.size()==49);
        Mesh combined=w.combinedMesh();validateMesh(combined);
        assert(combined.indices.size()/3<350000);
        size_t roads=0,paths=0,solids=0;bool foundBiome=false;
        for(const auto& chunk:w.chunks) {
            validateMesh(chunk.mesh);
            assert(chunk.mesh.indices.size()/3<350000);
            solids+=chunk.solids.size();
            for(const auto& box:chunk.solids) {
                assert(std::isfinite(box.min.x)&&std::isfinite(box.min.y)&&std::isfinite(box.min.z));
                assert(std::isfinite(box.max.x)&&std::isfinite(box.max.y)&&std::isfinite(box.max.z));
                assert(box.min.x<box.max.x&&box.min.y<box.max.y&&box.min.z<box.max.z);
            }
            for(int iz=0;iz<32;++iz)for(int ix=0;ix<32;++ix) {
                const float x=chunk.x*World::ChunkSize+ix*4.0f,z=chunk.z*World::ChunkSize+iz*4.0f;
                foundBiome=foundBiome||w.biome(x,z)==region.biome;
                if(!insideRoadCorridor(w,x,z))continue;
                Vec3 from{x,w.height(x,z),z};assert(!w.blocked(from,1.0f));++roads;
                for(Vec3 delta:{Vec3{4,0,0},Vec3{0,0,4}}) {
                    bool roadPath=true;
                    for(int step=1;step<=4;++step)
                        if(!insideRoadCorridor(w,x+delta.x*step*.25f,z+delta.z*step*.25f))roadPath=false;
                    if(!roadPath)continue;
                    const Vec3 target{x+delta.x,w.height(x+delta.x,z+delta.z),z+delta.z};
                    const Vec3 reached=w.move(from,target-from,1.0f);
                    assert(close(reached.x,target.x)&&close(reached.y,target.y)&&close(reached.z,target.z));
                    ++paths;
                }
            }
        }
        assert(foundBiome&&roads>0&&paths>0&&solids>0);
        // Leave every resident chunk behind, then require exact regeneration of all data.
        const auto snapshot=w.chunks;
        assert(w.stream({0,0,4608}));
        for(const auto& chunk:snapshot)for(const auto& away:w.chunks)
            assert(chunk.x!=away.x||chunk.z!=away.z);
        assert(w.stream(region.position));
        for(const auto& chunk:snapshot)compareChunks(chunk,find(w,chunk.x,chunk.z));
        std::printf("%s: %zu triangles, %zu collision solids, %zu clear road samples, %zu clear paths\n",
            region.name,combined.indices.size()/3,solids,roads,paths);
    }
}
bool hasSurfaceAt(const Mesh& mesh,Vec3 point,float tolerance=.08f,float material=-1) {
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1]];
        const auto& c=mesh.vertices[mesh.indices[i+2]];
        if(a.normal.y<.7f)continue;
        if(material>=0&&(a.material!=material||b.material!=material||c.material!=material))continue;
        const float det=(b.position.z-c.position.z)*(a.position.x-c.position.x)
            +(c.position.x-b.position.x)*(a.position.z-c.position.z);
        if(std::abs(det)<.000001f)continue;
        const float u=((b.position.z-c.position.z)*(point.x-c.position.x)
            +(c.position.x-b.position.x)*(point.z-c.position.z))/det;
        const float v=((c.position.z-a.position.z)*(point.x-c.position.x)
            +(a.position.x-c.position.x)*(point.z-c.position.z))/det;
        const float w=1-u-v;
        if(u<-.00001f||v<-.00001f||w<-.00001f)continue;
        if(close(point.y,u*a.position.y+v*b.position.y+w*c.position.y,tolerance))return true;
    }
    return false;
}
void vehicleSites() {
    World world;
    assert(world.waterDepth(8,8)==0);
    assert(world.waterDepth(3200,0)>1); // The elevated causeway does not replace the seabed.
    world.stream({2674,0,768});
    const Mesh dockMesh=world.combinedMesh();validateMesh(dockMesh);
    assert(dockMesh.indices.size()/3<350000);
    assert(close(world.height(2674,768),.4f));
    assert(world.waterDepth(2674,768)>.5f); // Depth remains available beneath the dock deck.
    assert(world.waterDepth(2678,768)>.5f);
    assert(hasSurfaceAt(dockMesh,{2678,World::WaterLevel,768},.08f,3));
    for(float x=2600;x<=2674;x+=2) {
        const Vec3 foot{x,world.height(x,768),768};
        assert(!world.blocked(foot,.45f));assert(hasSurfaceAt(dockMesh,foot));
        if(x<2674) {
            const Vec3 next{x+2,world.height(x+2,768),768};
            assert(length(world.move(foot,next-foot,.45f)-next)<.01f);
        }
    }
    for(float z:{756.0f,768.0f,780.0f}) {
        const Vec3 foot{2671,world.height(2671,z),z};
        assert(close(foot.y,.4f));assert(!world.blocked(foot,.45f));assert(hasSurfaceAt(dockMesh,foot));
    }
    world.stream({-3200,4,-1000});
    const Mesh runwayMesh=world.combinedMesh();validateMesh(runwayMesh);
    assert(runwayMesh.indices.size()/3<350000);
    assert(close(world.height(-3200,-1190),4));
    assert(!world.blocked({-3200,4,-1190},4));
    for(float z=-1256;z<=-744;z+=8)for(float dx:{-17.0f,0.0f,17.0f}) {
        const Vec3 foot{-3200+dx,world.height(-3200+dx,z),z};
        assert(close(foot.y,4));assert(!world.blocked(foot,1));
    }
    // Include exact chunk boundaries so missing runway segments cannot hide between samples.
    for(float z:{-1256.0f,-1152.0f,-1024.0f,-896.0f,-768.0f,-744.0f})
        for(float dx:{-17.0f,0.0f,17.0f})assert(hasSurfaceAt(runwayMesh,{-3200+dx,4,z},.08f,4));
    const Vec3 start{-3200,4,-1256},end{-3200,4,-744};
    assert(length(world.move(start,end-start,4)-end)<.01f);
    assert(length(world.move(end,start-end,4)-start)<.01f);
    std::puts("Vehicle sites: dock and seabed remain independent; 512 m runway clear in both directions");
}
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
    compareChunks(snapshot,restored);
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
int main(){geometry();geography();lighting();streamingAndSeams();naturalRegions();vehicleSites();collision();std::puts("World tests passed.");}
