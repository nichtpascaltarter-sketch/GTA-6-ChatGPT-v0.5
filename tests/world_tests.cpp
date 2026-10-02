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
        assert(close(length(v.normal),1,.001f));assert(v.material>=0&&v.material<=5);
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
    const auto compareLights=[](const std::vector<Light>& first,const std::vector<Light>& second) {
        assert(first.size()==second.size());
        for(size_t i=0;i<first.size();++i) {
            const auto& a=first[i];const auto& b=second[i];
            assert(sameVector(a.position,b.position)&&sameVector(a.direction,b.direction));
            assert(sameVector(a.color,b.color)&&a.radius==b.radius&&a.intensity==b.intensity&&a.cone==b.cone);
        }
    };
    compareLights(original.lights,restored.lights);compareLights(original.alwaysLights,restored.alwaysLights);
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
void groundSurfaces() {
    struct Sample {float x,z;GroundSurface surface;};
    const Sample samples[]={
        {8,8,GroundSurface::Pavement},{12,24,GroundSurface::Pavement},
        {14,64,GroundSurface::Pavement},{14.01f,64,GroundSurface::Grass},{16,64,GroundSurface::Pavement},
        {400,400,GroundSurface::Pavement},{3200,0,GroundSurface::Pavement},
        {-3200,-1256,GroundSurface::Pavement},{-3218,-1150,GroundSurface::Pavement},
        {-3218.01f,-1150,GroundSurface::Grass},{-3228,-1045,GroundSurface::Pavement},
        {-3130,-1024,GroundSurface::Pavement},{2560,768,GroundSurface::Soil},
        {2600,768,GroundSurface::Wood},{2675,764,GroundSurface::Wood},{2675.01f,768,GroundSurface::Soil},
        {1100,-2800,GroundSurface::Soil},{4140,-250,GroundSurface::Grass},
        {2600,800,GroundSurface::Sand},{-4200,1500,GroundSurface::Grass}
    };
    World world;
    for(const auto& sample:samples)assert(world.groundSurface(sample.x,sample.z)==sample.surface);
    assert(world.chunks.empty()); // Sound queries cannot require geometry residency or allocate chunks.
    assert(world.groundSurface(std::numeric_limits<float>::quiet_NaN(),0)==GroundSurface::Soil);
    assert(world.groundSurface(std::numeric_limits<float>::infinity(),0)==GroundSurface::Soil);
    for(int index=1;index<512;++index) {
        const Vec3 point=World::coastalRoadPoint(index/512.f);
        assert(world.groundSurface(point.x,point.z)==GroundSurface::Pavement);
    }
    world.stream({-2048,0,128});size_t paths=0;
    for(const auto& chunk:world.chunks)for(size_t index=0;index<chunk.mesh.indices.size();index+=3) {
        const auto& a=chunk.mesh.vertices[chunk.mesh.indices[index]];
        const auto& b=chunk.mesh.vertices[chunk.mesh.indices[index+1]];
        const auto& c=chunk.mesh.vertices[chunk.mesh.indices[index+2]];
        if(!sameVector(a.color,{.55f,.54f,.45f})||a.normal.y<.7f)continue;
        const Vec3 middle=(a.position+b.position+c.position)/3;
        assert(world.groundSurface(middle.x,middle.z)==GroundSurface::Pavement);++paths;
    }
    assert(paths>40);
    for(const auto& sample:samples)assert(world.groundSurface(sample.x,sample.z)==sample.surface);
    std::printf("Ground surfaces: paved roads, sidewalks and %zu path triangles; wood, soil, grass and sand independent of streaming\n",paths);
}
bool sourceOnEmissiveFace(const Mesh& m,Vec3 p,float material=2) {
    for(size_t i=0;i<m.indices.size();i+=3) {
        const Vertex& a=m.vertices[m.indices[i]];const Vertex& b=m.vertices[m.indices[i+1]];const Vertex& c=m.vertices[m.indices[i+2]];
        if(a.material!=material || b.material!=material || c.material!=material || a.normal.y>-.999f)continue;
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
void rescueLaunch() {
    const Vec3 position{3090,World::WaterLevel,1080};World world;world.stream(position);
    const Chunk& owner=find(world,24,8);assert(owner.solids.size()==4&&owner.lights.size()==1);validateMesh(owner.mesh);
    assert(world.waterDepth(position.x,position.z)>1&&world.biome(position.x,position.z)==Biome::Ocean);
    assert(world.blocked(position,1.1f));
    for(float distance:{9.0f,12.0f,14.0f})for(Vec3 direction:{Vec3{-1,0,0},Vec3{0,0,-1}}) {
        Vec3 approach=position+direction*distance;assert(!world.blocked(approach,1.2f));
        assert(world.waterDepth(approach.x,approach.z)>1);
        Vec3 from=position+direction*22,result=world.move(from,approach-from,1.2f);
        assert(length(result-approach)<.01f);
    }
    const Vec3 westHit=world.move(position+Vec3{-22,0,0},{44,0,0},1.2f);
    const Vec3 southHit=world.move(position+Vec3{0,0,-22},{0,0,44},1.2f);
    assert(westHit.x<position.x-2.8f&&westHit.x>position.x-3.0f);
    assert(southHit.z<position.z-6.2f&&southHit.z>position.z-6.4f);
    const Light& beacon=owner.lights.front();
    assert(beacon.radius==18&&beacon.intensity==32&&beacon.cone==-1);
    assert(sameVector(beacon.color,{1,.12f,.055f}));assert(sourceOnEmissiveFace(owner.mesh,beacon.position));
    bool mapped=false;for(const Landmark& landmark:World::landmarks())if(std::strcmp(landmark.name,"Leena's Launch")==0){mapped=true;assert(sameVector(landmark.position,position));}
    assert(mapped);const Chunk snapshot=owner;
    world.stream({8,0,8});world.stream(position);compareChunks(snapshot,find(world,24,8));
    std::printf("Clinic launch: %zu owner-chunk triangles, four collision sections, attached red hazard beacon\n",snapshot.mesh.indices.size()/3);
}
float solidRay(const World& world,Vec3 start,Vec3 target) {
    const Vec3 direction=normalized(target-start);float hit=length(target-start);
    for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids) {
        float enter=0,leave=hit;bool intersects=true;
        const float p[]={start.x,start.y,start.z},d[]={direction.x,direction.y,direction.z};
        const float lo[]={box.min.x,box.min.y,box.min.z},hi[]={box.max.x,box.max.y,box.max.z};
        for(int axis=0;axis<3;++axis) {
            if(std::abs(d[axis])<.000001f) {if(p[axis]<lo[axis]||p[axis]>hi[axis])intersects=false;continue;}
            const float a=(lo[axis]-p[axis])/d[axis],b=(hi[axis]-p[axis])/d[axis];
            enter=std::max(enter,std::min(a,b));leave=std::min(leave,std::max(a,b));
            if(enter>leave)intersects=false;
        }
        if(intersects)hit=std::min(hit,enter);
    }
    return hit;
}
void workshop() {
    static_assert(sizeof(Light)==48,"The GPU light format must stay unchanged");
    const GarageSite site=World::garageSite();World world;world.stream(site.marker);
    assert(std::strcmp(site.name,"Harbor Motor Works")==0);
    assert(close(site.floorHeight,.16f)&&close(site.vehicleHeading,Pi));
    assert(close(world.height(site.marker.x,site.marker.z),site.marker.y,.00001f));
    const Chunk& owner=find(world,1,0);validateMesh(owner.mesh);
    assert(owner.alwaysLights.size()>=6&&owner.alwaysLights.size()<=10);
    size_t allDaySources=0;for(const auto& chunk:world.chunks)allDaySources+=chunk.alwaysLights.size();
    assert(allDaySources==owner.alwaysLights.size());
    for(const Light& light:owner.alwaysLights) {
        assert(light.position.x>site.shell.min.x&&light.position.x<site.shell.max.x);
        assert(light.position.z>site.shell.min.z&&light.position.z<site.shell.max.z);
        assert(light.position.y>2.7f&&light.position.y<site.shell.max.y);
        assert(std::isfinite(light.radius)&&light.radius>2&&light.radius<=16);
        assert(std::isfinite(light.intensity)&&light.intensity>0&&light.intensity<=256);
        assert(std::isfinite(light.color.x)&&std::isfinite(light.color.y)&&std::isfinite(light.color.z));
        assert(light.color.x>=0&&light.color.x<=1&&light.color.y>=0&&light.color.y<=1&&light.color.z>=0&&light.color.z<=1);
        assert(light.cone>=0&&light.cone<1&&sameVector(light.direction,{0,-1,0}));
        assert(sourceOnEmissiveFace(owner.mesh,light.position,5));
    }
    Chunk allocated;const size_t emptyBytes=World::chunkBytes(allocated);allocated.alwaysLights.reserve(64);
    assert(World::chunkBytes(allocated)==emptyBytes+allocated.alwaysLights.capacity()*sizeof(Light));
    for(Vec3 point:{site.vehicleStop,site.counter,site.staff,site.vehicleDoor,site.pedestrianDoor}) {
        assert(close(world.height(point.x,point.z),site.floorHeight,.00001f));
        assert(world.groundSurface(point.x,point.z)==GroundSurface::Pavement);
        assert(!world.blocked(point,.34f));assert(hasSurfaceAt(owner.mesh,point,.025f));
    }
    for(int sample=0;sample<=100;++sample) {
        const float z=98+sample*.2f,h=world.height(175,z);
        assert(close(h,.16f*(118-z)/20,.00001f));
        assert(world.groundSurface(175,z)==GroundSurface::Pavement);
        assert(std::abs(h-world.height(175,z+.001f))<.00002f);
        assert(hasSurfaceAt(owner.mesh,{175,h,z},.025f));
    }
    for(float z:{74.f,78.f,88.f,98.f,108.f,118.f})for(float x:{146.f,154.f,175.f,184.f,190.f}) {
        const float h=world.height(x,z);
        assert(std::abs(h-world.height(x+.001f,z))<.0001f);
        assert(std::abs(h-world.height(x,z+.001f))<.0001f);
    }
    size_t routeSamples=0;
    const auto traverse=[&](Vec3 start,Vec3 target,float radius) {
        start.y=world.height(start.x,start.z);target.y=world.height(target.x,target.z);
        Vec3 current=start;
        for(int sample=1;sample<=128;++sample) {
            Vec3 point=start+(target-start)*(sample/128.f);point.y=world.height(point.x,point.z);
            assert(!world.blocked(point,radius));
            const Vec3 reached=world.move(current,point-current,radius);
            assert(length(reached-point)<.005f);current=point;++routeSamples;
        }
        const Vec3 swept=world.move(start,target-start,radius);assert(length(swept-target)<.005f);
    };
    traverse(site.streetAccess,site.vehicleStop,1.02f);traverse(site.vehicleStop,site.streetAccess,1.02f);
    traverse({160,0,120},site.pedestrianDoor,.34f);traverse(site.pedestrianDoor,site.counter,.34f);
    traverse(site.counter,site.pedestrianDoor,.34f);traverse(site.pedestrianDoor,{160,0,120},.34f);
    for(float x:{173.f,177.f})traverse(site.vehicleStop,{x,site.floorHeight,88},.35f);
    traverse({173,0,88},{173,0,100},.34f);traverse({173,0,100},{160,0,100},.34f);
    traverse({160,0,100},site.counter,.34f);
    // Thin real walls must stop a fast crossing; open portals must not stop a ray.
    for(float x:{156.f,165.f,182.f}) {
        const Vec3 start{x,site.floorHeight,105},target{x,site.floorHeight,90};
        const Vec3 reached=world.move(start,target-start,.34f);
        assert(reached.z>98&&reached.z<99.5f);
        assert(solidRay(world,start+Vec3{0,1.4f,0},target+Vec3{0,1.4f,0})<8);
    }
    for(float x:{160.f,175.f}) {
        const Vec3 start{x,site.floorHeight+1.4f,102},target{x,site.floorHeight+1.4f,90};
        assert(close(solidRay(world,start,target),length(target-start)));
    }
    const Vec3 roofStart=site.vehicleStop+Vec3{0,2,0};
    assert(solidRay(world,roofStart,roofStart+Vec3{0,12,0})<6);
    bool mapped=false;for(const Landmark& mark:World::landmarks())if(std::strcmp(mark.name,site.name)==0) {
        assert(length(mark.position-site.marker)<.005f);mapped=true;
    }
    assert(mapped);
    for(Vec3 mission:{Vec3{12,0,24},Vec3{268,0,128}})assert(!world.blocked(mission,.45f));
    const Chunk snapshot=owner;world.stream({4096,0,2048});world.stream(site.counter);
    assert(world.collisionReady(site.counter));compareChunks(snapshot,find(world,1,0));
    assert(!world.blocked(site.counter,.34f)&&!world.blocked(site.vehicleStop,1.02f));
    const size_t triangles=world.combinedMesh().indices.size()/3;assert(triangles<350000);
    std::printf("Harbor Motor Works: %zu clear route samples, %zu all-day lights, %zu owner triangles, %zu loaded triangles\n",
        routeSamples,snapshot.alwaysLights.size(),snapshot.mesh.indices.size()/3,triangles);
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
    for(int sample=1;sample<512;++sample){const Vec3 p=World::coastalRoadPoint(sample/512.f);assert(w.road(p.x,p.z));}
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
int main(){geometry();geography();lighting();rescueLaunch();workshop();streamingAndSeams();naturalRegions();vehicleSites();groundSurfaces();collision();std::puts("World tests passed.");}
