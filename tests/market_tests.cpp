#include "../src/world.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace mc;
namespace {
constexpr uint64_t BaselinePlaces=5282381464599706483ull;
constexpr uint64_t BaselineNavigation=8070828538268000561ull;
struct Fingerprint {
    uint64_t hash=1469598103934665603ull;
    void value(uint64_t v){for(int i=0;i<8;++i){hash^=(v>>(i*8))&255;hash*=1099511628211ull;}}
    void vector(Vec3 p){value(uint64_t(std::llround(p.x*1000)));value(uint64_t(std::llround(p.y*1000)));value(uint64_t(std::llround(p.z*1000)));}
};
bool close(float a,float b,float epsilon=.002f){return std::abs(a-b)<=epsilon;}
bool hasVertex(const Mesh& mesh,Vec3 p){for(const auto& v:mesh.vertices)if(length(v.position-p)<.002f)return true;return false;}
const Chunk& origin(const World& world){for(const auto& chunk:world.chunks)if(chunk.x==0&&chunk.z==0)return chunk;assert(false);return world.chunks.front();}
bool horizontalSurface(const Mesh& mesh,float x,float y,float z,Vec3* color=nullptr,float* material=nullptr) {
    const auto cross2=[](Vec3 a,Vec3 b,Vec3 p){return (b.x-a.x)*(p.z-a.z)-(b.z-a.z)*(p.x-a.x);};
    const Vec3 p{x,y,z};
    for(size_t i=0;i<mesh.indices.size();i+=3){
        const auto& a=mesh.vertices[mesh.indices[i]];const auto& b=mesh.vertices[mesh.indices[i+1]];const auto& c=mesh.vertices[mesh.indices[i+2]];
        if(!close(a.position.y,y,.0005f)||!close(b.position.y,y,.0005f)||!close(c.position.y,y,.0005f))continue;
        const float ab=cross2(a.position,b.position,p),bc=cross2(b.position,c.position,p),ca=cross2(c.position,a.position,p);
        if((ab>=-.00001f&&bc>=-.00001f&&ca>=-.00001f)||(ab<=.00001f&&bc<=.00001f&&ca<=.00001f)){
            if(color)*color=a.color;
            if(material)*material=a.material;
            return true;
        }
    }
    return false;
}
void unchangedNeighborhood(World& world) {
    const auto& places=World::pedestrianPlaces();assert(places.size()==54);
    Fingerprint catalog;
    for(const auto& p:places){
        catalog.value(p.id);catalog.value(p.siteId);catalog.value(uint64_t(p.kind));
        catalog.vector(p.position);catalog.vector(p.approach);catalog.vector(p.seatPosition);
        catalog.value(uint64_t(std::llround(p.yaw*1000)));catalog.value(p.capacity);catalog.value(p.sheltered);
    }
    // Captured from source 1a7241b before the market change. Quantization is one
    // millimetre and one milliradian, independent of object layout or byte order.
    assert(catalog.hash==BaselinePlaces);
    const auto graph=world.pedestrianNetwork({8,0,8});assert(graph.nodes.size()==252&&graph.edges.size()==369&&graph.places.size()==54);
    Fingerprint navigation;auto nodes=graph.nodes;
    std::sort(nodes.begin(),nodes.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(const auto& node:nodes){navigation.value(node.id);navigation.vector(node.position);navigation.value(uint64_t(node.kind));}
    std::vector<std::array<uint32_t,3>> edges;size_t crossings=0,samples=0;
    for(const auto& edge:graph.edges){
        uint32_t a=graph.nodes[edge.from].id,b=graph.nodes[edge.to].id;if(a>b)std::swap(a,b);
        edges.push_back({a,b,edge.crossingId});crossings+=edge.crossingId!=0;
        const Vec3 from=graph.nodes[edge.from].position,to=graph.nodes[edge.to].position;
        assert(length(world.move(from,to-from,.28f)-to)<.006f);
        assert(length(world.move(to,from-to,.28f)-from)<.006f);
        const int count=std::max(1,int(std::ceil(length(to-from)*2)));
        for(int i=0;i<=count;++i){const Vec3 p=from+(to-from)*(float(i)/count);assert(!world.blocked(p,.28f));++samples;}
    }
    std::sort(edges.begin(),edges.end());for(const auto& edge:edges)for(uint32_t value:edge)navigation.value(value);
    assert(navigation.hash==BaselineNavigation&&crossings==24);
    size_t seats=0;
    for(const auto& place:places){
        assert(length(world.move(place.approach,place.position-place.approach,.28f)-place.position)<.006f);
        if(place.kind==PedestrianPlaceKind::Seat){
            ++seats;const Vec3 surface=place.seatPosition-Vec3{0,.11f,0};
            assert(horizontalSurface(origin(world).mesh,surface.x,surface.y,surface.z));
        }
    }
    assert(seats==8);
    std::printf("Unchanged neighborhood: %zu places, %zu nodes, %zu edges, %zu crossings, %zu seat surfaces, %zu clear samples\n",
        places.size(),nodes.size(),edges.size(),crossings,seats,samples);
}
void circulationAndCountertops(World& world) {
    const Mesh& mesh=origin(world).mesh;size_t clear=0;
    const auto traverse=[&](Vec3 a,Vec3 b){
        assert(length(world.move(a,b-a,.34f)-b)<.006f);
        assert(length(world.move(b,a-b,.34f)-a)<.006f);
        const int count=std::max(1,int(std::ceil(length(b-a)*4)));
        for(int i=0;i<=count;++i){const Vec3 p=a+(b-a)*(float(i)/count);assert(!world.blocked(p,.34f));++clear;}
    };
    for(float x:{81.f,82.f,83.5f})traverse({x,0,25},{x,0,82});
    for(float z:{35.f,54.f,73.f}){
        traverse({83.5f,0,z},{85.5f,0,z});
        assert(!world.blocked({85.5f,0,z},.34f));assert(world.blocked({87.5f,0,z},.34f));
        assert(!world.blocked({87.5f,1.16f,z},.28f)); // Counter collision stops at its actual top.
        for(float x:{86.5f,87.5f,88.5f})for(float offset:{-2.2f,0.f,2.2f})assert(horizontalSurface(mesh,x,1.15f,z+offset));
        for(float offset:{-6.8f,6.8f})assert(world.blocked({88.75f,0,z+offset},.2f));
        assert(!world.blocked({84.5f,0,z},.34f));
        assert(world.blocked({84.5f,3.0f,z},.34f)); // The roof, not an invisible full-height box.
    }
    // Each display has an actual load path to its counter: wood crate floor,
    // ceramic tea service on a cloth, and two bread trays.
    for(float z:{33.5f,35.f,36.5f})assert(horizontalSurface(mesh,87.45f,1.24f,z));
    assert(horizontalSurface(mesh,87.13f,1.19f,54.15f));
    for(float z:{71.9f,74.1f})assert(horizontalSurface(mesh,87.5f,1.24f,z));
    // Different commodity silhouettes and palettes are present above the supports.
    size_t citrus=0,ceramic=0,bread=0;
    for(const auto& vertex:mesh.vertices){const auto p=vertex.position,c=vertex.color;
        if(p.x<86.2f||p.x>88.7f||p.y<1.24f||p.y>2.0f)continue;
        if(p.z>32.5f&&p.z<37.5f&&c.x>.90f&&c.y>.40f&&c.y<.50f)++citrus;
        if(p.z>51.5f&&p.z<56.5f&&close(c.x,.12f)&&close(c.y,.34f)&&close(c.z,.39f))++ceramic;
        if(p.z>70.5f&&p.z<75.5f&&close(c.x,.73f)&&close(c.y,.39f)&&close(c.z,.13f))++bread;
    }
    assert(citrus>=100&&ceramic>=100&&bread>=100);
    std::printf("Market circulation: %zu clear player samples; three solid countertops and grounded fruit, tea and bread displays\n",clear);
}
void lightsAndLod(const World& world) {
    const auto site=World::marketSite();assert(std::strcmp(site.name,"Tide Hall Market")==0);
    assert(site.bounds.min.x==15&&site.bounds.min.z==15&&site.bounds.max.x==113&&site.bounds.max.z==113);
    assert(site.bounds.max.y==27.7f&&site.fountain.x==64&&site.fountain.z==64&&site.tower.x==44&&site.tower.z==98);
    bool landmark=false;for(const auto& item:World::landmarks())if(std::strcmp(item.name,site.name)==0){assert(length(item.position-site.marker)<.001f);landmark=true;}assert(landmark);
    const Chunk& detailed=origin(world);assert(detailed.lights.size()==19&&detailed.alwaysLights.empty());
    for(float z:{29.f,46.f,63.f,80.f}){
        size_t found=0;
        for(const auto& light:detailed.lights)if(length(light.position-Vec3{84.45f,4.48f,z})<.001f){
            ++found;Vec3 color;float material=0;
            assert(horizontalSurface(detailed.mesh,light.position.x,light.position.y,light.position.z,&color,&material));
            assert(material==2&&length(color-light.color)<.001f&&light.radius==13&&light.intensity==30);
            assert(length(light.direction-Vec3{0,-1,0})<.001f&&close(light.cone,.2f));
        }
        assert(found==1);
    }
    std::array<size_t,3> triangles{},bytes{};
    for(WorldLod lod:{WorldLod::Detail,WorldLod::Medium,WorldLod::Far}){
        const auto chunk=World::buildChunk({0,0,0,0,lod}).chunk;const Mesh& mesh=chunk.mesh;
        triangles[size_t(lod)]=mesh.indices.size()/3;bytes[size_t(lod)]=World::chunkBytes(chunk);
        assert(hasVertex(mesh,{44,27.7f,98}));
        assert(hasVertex(mesh,{65.375f,7.40f,65.025f}));
        for(float z:{35.f,54.f,73.f}){
            for(float side:{-1.f,1.f}){
                assert(hasVertex(mesh,{96,7.6f,z+side*8.3f}));
                assert(hasVertex(mesh,{83.6f,4.6f,z+side*8.2f}));
                assert(hasVertex(mesh,{89.3f,5.1f,z+side*8.2f}));
            }
        }
        assert(hasVertex(mesh,{83.44f,5.88f,47.3f}));
        for(size_t i=0;i<mesh.indices.size();i+=3){
            const Vec3 a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
            const Vec3 area=cross(b-a,c-a);assert(std::isfinite(dot(area,area))&&dot(area,area)>0);
        }
        for(const auto& vertex:mesh.vertices){assert(close(length(vertex.normal),1,.001f));assert(std::isfinite(vertex.position.x)&&std::isfinite(vertex.position.y)&&std::isfinite(vertex.position.z));}
        if(lod!=WorldLod::Detail){assert(chunk.solids.empty()&&chunk.lights.empty()&&chunk.alwaysLights.empty());assert(bytes[size_t(lod)]<World::MaxVisualChunkBytes);}
    }
    const size_t combined=world.combinedMesh().indices.size()/3;
    assert(combined<=347741+1800&&combined<350000);
    assert(triangles[0]>triangles[1]&&triangles[1]>triangles[2]);
    std::printf("Market LOD triangles: %zu/%zu/%zu; owner capacity bytes %zu/%zu/%zu; loaded triangles %zu, net %+lld\n",
        triangles[0],triangles[1],triangles[2],bytes[0],bytes[1],bytes[2],combined,static_cast<long long>(combined)-347741);
}
}
int main(){World world;world.stream({8,0,8});unchangedNeighborhood(world);circulationAndCountertops(world);lightsAndLod(world);std::puts("Tide Hall Market tests passed.");}
