#include "../src/world_geometry.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <utility>

using namespace mc;
using namespace mc::worldGeometry;
namespace {
constexpr std::array<WorldLod,3> Lods{WorldLod::Detail,WorldLod::Medium,WorldLod::Far};
constexpr std::array<size_t,3> TerrainTriangles{512,160,80};
// Flat city, signed coordinates, hills, wetlands, islands, coast, bridge and airfield.
constexpr std::array<std::pair<int,int>,15> Samples{{
    {0,0},{-1,-1},{-33,15},{8,-22},{31,-2},{19,5},{25,0},{-25,-8},{42,-18},{48,48},
    {3,3},{-4,2},{20,5},{24,8},{-26,-8}
}};
bool same(Vec3 a,Vec3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool close(float a,float b,float tolerance=.002f) {return std::abs(a-b)<=tolerance;}
void validateMesh(const Mesh& mesh) {
    assert(!mesh.vertices.empty()&&!mesh.indices.empty()&&mesh.indices.size()%3==0);
    for(uint32_t index:mesh.indices)assert(index<mesh.vertices.size());
    for(const Vertex& vertex:mesh.vertices) {
        assert(std::isfinite(vertex.position.x)&&std::isfinite(vertex.position.y)&&std::isfinite(vertex.position.z));
        assert(std::isfinite(vertex.normal.x)&&std::isfinite(vertex.normal.y)&&std::isfinite(vertex.normal.z));
        assert(std::isfinite(vertex.color.x)&&std::isfinite(vertex.color.y)&&std::isfinite(vertex.color.z));
        assert(close(length(vertex.normal),1));
        assert(std::isfinite(vertex.material)&&vertex.material>=0&&vertex.material<=4);
    }
}
void validateTriangleAreas(const Chunk& chunk) {
    const Mesh& mesh=chunk.mesh;
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        const Vec3 a=mesh.vertices[mesh.indices[i]].position;
        const Vec3 b=mesh.vertices[mesh.indices[i+1]].position;
        const Vec3 c=mesh.vertices[mesh.indices[i+2]].position;
        const Vec3 area=cross(b-a,c-a);const float squaredArea=dot(area,area);
        if(!std::isfinite(squaredArea)||squaredArea<=0)
            std::fprintf(stderr,"Degenerate coarse triangle in chunk (%d,%d), triangle %zu\n",chunk.x,chunk.z,i/3);
        assert(std::isfinite(squaredArea)&&squaredArea>0);
    }
}
void compareMeshes(const Mesh& a,const Mesh& b) {
    assert(a.vertices.size()==b.vertices.size()&&a.indices==b.indices);
    for(size_t i=0;i<a.vertices.size();++i) {
        const Vertex& av=a.vertices[i];const Vertex& bv=b.vertices[i];
        assert(same(av.position,bv.position)&&same(av.normal,bv.normal));
        assert(same(av.color,bv.color)&&av.material==bv.material);
    }
}
using Point=std::array<float,3>;
using Edge=std::pair<Point,Point>;
using Boundary=std::map<Edge,int>;
using Boundaries=std::array<Boundary,4>;
struct EdgeUse {unsigned count=0;int orientation=0;};
Point point(Vec3 p) {return {p.x,p.y,p.z};}
Boundaries terrainTopology(const Mesh& mesh,int cx,int cz,size_t triangles) {
    validateMesh(mesh);assert(mesh.indices.size()/3==triangles);
    const float x0=cx*World::ChunkSize,x1=x0+World::ChunkSize;
    const float z0=cz*World::ChunkSize,z1=z0+World::ChunkSize;
    std::map<Edge,EdgeUse> edges;
    double projectedArea=0;
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        const Vec3 a=mesh.vertices[mesh.indices[i]].position;
        const Vec3 b=mesh.vertices[mesh.indices[i+1]].position;
        const Vec3 c=mesh.vertices[mesh.indices[i+2]].position;
        const float twiceArea=cross(b-a,c-a).y;
        assert(twiceArea>0);projectedArea+=double(twiceArea)*.5;
        for(Vec3 p:{a,b,c})assert(p.x>=x0&&p.x<=x1&&p.z>=z0&&p.z<=z1);
        const std::array<Point,3> vertices{point(a),point(b),point(c)};
        for(size_t j=0;j<3;++j) {
            Point first=vertices[j],second=vertices[(j+1)%3];
            assert(first!=second);int direction=1;
            if(second<first){std::swap(first,second);direction=-1;}
            EdgeUse& use=edges[{first,second}];++use.count;use.orientation+=direction;
        }
    }
    assert(projectedArea==double(World::ChunkSize)*World::ChunkSize);
    Boundaries result;
    for(const auto& [edge,use]:edges) {
        assert(use.count==1||use.count==2);
        if(use.count==2){assert(use.orientation==0);continue;}
        int side=-1;
        if(edge.first[0]==x0&&edge.second[0]==x0)side=0;
        else if(edge.first[0]==x1&&edge.second[0]==x1)side=1;
        else if(edge.first[2]==z0&&edge.second[2]==z0)side=2;
        else if(edge.first[2]==z1&&edge.second[2]==z1)side=3;
        // An unmatched interior edge also detects a long edge meeting shorter edges.
        assert(side>=0);assert(std::abs(use.orientation)==1);
        result[static_cast<size_t>(side)].emplace(edge,use.orientation);
    }
    for(size_t side=0;side<result.size();++side) {
        assert(result[side].size()==16);
        std::array<bool,16> seen{};
        const size_t axis=side<2?2:0;const float base=side<2?z0:x0;
        for(const auto& [edge,orientation]:result[side]) {
            (void)orientation;
            const float lo=std::min(edge.first[axis],edge.second[axis]);
            const float hi=std::max(edge.first[axis],edge.second[axis]);
            assert(hi-lo==8);
            const float offset=(lo-base)/8;const int segment=static_cast<int>(offset);
            assert(offset==float(segment)&&segment>=0&&segment<16);
            assert(!seen[static_cast<size_t>(segment)]);seen[static_cast<size_t>(segment)]=true;
        }
        for(bool present:seen)assert(present);
    }
    return result;
}
void compareSeam(const Boundary& first,const Boundary& second) {
    assert(first.size()==second.size());
    auto a=first.begin(),b=second.begin();
    for(;a!=first.end();++a,++b) {
        // Includes exact heights; merely finding the same x/z vertices is insufficient.
        assert(a->first==b->first);assert(a->second==-b->second);
    }
}
void terrainSeams() {
    World world;size_t comparisons=0;
    for(const auto& [x,z]:Samples) {
        std::array<std::array<Boundaries,3>,3> sides;
        std::array<std::array<std::map<Point,Vec3>,3>,3> normals;
        std::array<bool,3> wet{};
        for(size_t neighbor=0;neighbor<3;++neighbor)for(size_t lod=0;lod<Lods.size();++lod) {
            const int nx=x+(neighbor==1?1:0),nz=z+(neighbor==2?1:0);
            Mesh mesh;const bool hasWater=appendTerrain(mesh,world,nx,nz,Lods[lod]);
            if(lod==0)wet[neighbor]=hasWater;else assert(wet[neighbor]==hasWater);
            sides[neighbor][lod]=terrainTopology(mesh,nx,nz,TerrainTriangles[lod]);
            for(const Vertex& vertex:mesh.vertices) {
                const auto [existing,inserted]=normals[neighbor][lod].emplace(point(vertex.position),vertex.normal);
                if(!inserted)assert(same(existing->second,vertex.normal));
            }
            if(lod>0)for(size_t side=0;side<4;++side)assert(sides[neighbor][lod][side]==sides[neighbor][0][side]);
        }
        for(size_t a=0;a<3;++a)for(size_t b=0;b<3;++b) {
            compareSeam(sides[0][a][1],sides[1][b][0]);
            compareSeam(sides[0][a][3],sides[2][b][2]);comparisons+=2;
            for(size_t neighbor=1;neighbor<=2;++neighbor)for(const auto& [edge,orientation]:sides[0][a][neighbor==1?1:3]) {
                (void)orientation;
                for(const Point& p:{edge.first,edge.second})assert(same(normals[0][a].at(p),normals[neighbor][b].at(p)));
            }
        }
    }
    // appendTerrain must respect existing vertex indices when appending to a mesh.
    Mesh single;appendTerrain(single,world,-33,15,WorldLod::Far);
    Mesh combined=single;appendTerrain(combined,world,-32,15,WorldLod::Medium);
    assert(combined.vertices.size()>single.vertices.size());
    for(size_t i=0;i<single.indices.size();++i)assert(combined.indices[i]==single.indices[i]);
    for(size_t i=single.indices.size();i<combined.indices.size();++i)assert(combined.indices[i]>=single.vertices.size());
    for(size_t i=0;i<single.vertices.size();++i) {
        assert(same(combined.vertices[i].position,single.vertices[i].position));
        assert(same(combined.vertices[i].normal,single.vertices[i].normal));
    }
    std::printf("Terrain: 512/160/80 triangles, canonical eight-metre boundaries, %zu mixed-LOD seams; no unmatched interior edges\n",comparisons);
}
Chunk chunkAt(int x,int z,WorldLod lod) {
    const ChunkBuildRequest request{x,z,73,91,lod};
    ChunkBuildResult result=World::buildChunk(request);
    assert(result.request.x==x&&result.request.z==z&&result.request.epoch==73&&result.request.ticket==91&&result.request.lod==lod);
    assert(result.chunk.x==x&&result.chunk.z==z);return std::move(result.chunk);
}
void chunkDeterminism() {
    for(const auto& [x,z]:Samples) {
        size_t previous=std::numeric_limits<size_t>::max();
        for(WorldLod lod:Lods) {
            const Chunk first=chunkAt(x,z,lod),second=chunkAt(x,z,lod);
            validateMesh(first.mesh);compareMeshes(first.mesh,second.mesh);
            assert(first.mesh.indices.size()<previous);previous=first.mesh.indices.size();
            assert(same(first.bounds.min,second.bounds.min)&&same(first.bounds.max,second.bounds.max));
            for(const Vertex& vertex:first.mesh.vertices) {
                const Vec3 p=vertex.position;
                assert(p.x>=first.bounds.min.x&&p.y>=first.bounds.min.y&&p.z>=first.bounds.min.z);
                assert(p.x<=first.bounds.max.x&&p.y<=first.bounds.max.y&&p.z<=first.bounds.max.z);
            }
            if(lod!=WorldLod::Detail) {
                validateTriangleAreas(first);
                assert(first.solids.empty()&&first.lights.empty());
                assert(World::chunkBytes(first)<=World::MaxVisualChunkBytes);
            }
        }
    }
    std::printf("Chunks: deterministic geometry and bounds in %zu regions; visual-only LODs stay within per-tile capacity budget\n",Samples.size());
}
float skylineTop(const Chunk& chunk,const BuildingSpec& building) {
    float top=-std::numeric_limits<float>::infinity();
    for(const Vertex& vertex:chunk.mesh.vertices) {
        const Vec3 p=vertex.position;
        if(std::abs(p.x-building.position.x)<=building.halfWidth+2&&
           std::abs(p.z-building.position.z)<=building.halfDepth+2)top=std::max(top,p.y);
    }
    return top;
}
bool hasVertex(const Chunk& chunk,Vec3 point) {
    for(const Vertex& vertex:chunk.mesh.vertices)if(length(vertex.position-point)<.003f)return true;
    return false;
}
void bodySilhouette(const Chunk& chunk,const BuildingSpec& building) {
    const Vec3 p=building.position;const float hx=building.halfWidth,hz=building.halfDepth,h=building.height;
    if(building.suburban) {
        for(float sign:{-1.0f,1.0f})assert(hasVertex(chunk,p+Vec3{0,h+2.6f,sign*(hz+.7f)}));
    } else if(building.style==0||building.style==1) {
        // Retain the narrowing three-tier mass, not a single box with the same roof height.
        for(float sign:{-1.0f,1.0f}) {
            assert(hasVertex(chunk,p+Vec3{sign*hx,h*.62f+4.7f*.38f,sign*hz}));
            assert(hasVertex(chunk,p+Vec3{sign*hx*.78f,h*.81f+4.7f*.19f,sign*hz*.78f}));
            assert(hasVertex(chunk,p+Vec3{sign*hx*.78f*.78f,h,sign*hz*.78f*.78f}));
        }
    } else if(building.style==4&&h>20) {
        const float radius=std::min(hx,hz)*.96f;
        for(Vec3 direction:{Vec3{1,0,0},Vec3{0,0,1},Vec3{-1,0,0},Vec3{0,0,-1}})
            assert(hasVertex(chunk,p+direction*radius+Vec3{0,h,0}));
    } else if(building.style==2) {
        for(int roof=0;roof<3;++roof)for(float sign:{-1.0f,1.0f})
            assert(hasVertex(chunk,p+Vec3{-hx+(roof+.5f)*hx*2/3,h+3.2f,sign*(hz+.5f)}));
    } else if(building.style==5) {
        for(float sign:{-1.0f,1.0f})assert(hasVertex(chunk,p+Vec3{0,h+6.5f,sign*(hz+.6f)}));
    }
}
void skylineShapes() {
    for(uint32_t style=0;style<6;++style) {
        const BuildingSpec building=describeBuilding({12,34,56},15,16,80,style,false);
        assert(building.style==static_cast<int>(style));assert(same(building.position,{12,34,56}));
        assert(building.halfWidth==15&&building.halfDepth==16);
        assert(building.height==(style==2?22.5f:style==5?28.0f:80.0f));
        const BuildingSpec house=describeBuilding({12,34,56},15,16,80,style,true);
        assert(house.suburban&&house.height==80);
    }
    World world;std::array<bool,6> seen{};bool houseSeen=false;size_t checked=0;
    const std::array<float,6> roofRise{9.6f,11.4f,6.0f,1.8f,7.85f,6.5f};
    for(int z=-4;z<=4;++z)for(int x=-4;x<=4;++x) {
        const BlockSpec block=describeBlock(world,x,z);
        if(!block.urban||block.plaza)continue;
        for(const BuildingSpec& building:block.buildings) {
            assert(!building.suburban&&building.style>=0&&building.style<6);
            const size_t style=static_cast<size_t>(building.style);
            if(seen[style]||(style==4&&building.height<=20))continue;
            const float expected=building.position.y+building.height+roofRise[style];
            for(WorldLod lod:Lods) {
                const Chunk chunk=chunkAt(x,z,lod);
                const float actual=skylineTop(chunk,building);
                if(!close(actual,expected))std::fprintf(stderr,"Skyline mismatch style=%zu lod=%u chunk=(%d,%d) expected=%.4f actual=%.4f\n",style,unsigned(lod),x,z,expected,actual);
                assert(close(actual,expected));bodySilhouette(chunk,building);++checked;
            }
            seen[style]=true;
        }
    }
    for(bool present:seen)assert(present);
    for(int x=-20;x<=-12&&!houseSeen;++x) {
        const BlockSpec block=describeBlock(world,x,0);
        if(!block.urban||block.plaza||block.buildings.empty())continue;
        const BuildingSpec& building=block.buildings.front();if(!building.suburban)continue;
        for(WorldLod lod:Lods) {
            const Chunk chunk=chunkAt(x,0,lod);
            assert(close(skylineTop(chunk,building),building.position.y+building.height+3));bodySilhouette(chunk,building);++checked;
        }
        houseSeen=true;
    }
    assert(houseSeen);
    std::printf("Skyline: all six city styles and suburban chimney/roof maxima preserved across %zu LOD samples\n",checked);
}
void landmarkShapes() {
    const World world;
    const float exchangeGround=world.height(448,448),plazaGround=world.height(64,64);
    for(WorldLod lod:Lods) {
        const Chunk exchange=chunkAt(3,3,lod),plaza=chunkAt(0,0,lod);
        assert(hasVertex(exchange,{448,exchangeGround+119,448}));
        assert(hasVertex(plaza,{44,plazaGround+27.7f,98}));
        for(int shop=0;shop<3;++shop)for(float sign:{-1.0f,1.0f})
            assert(hasVertex(plaza,{96,plazaGround+7.6f,35+shop*19+sign*8.3f}));
        const Chunk hangar=chunkAt(-26,-8,lod);
        for(float sign:{-1.0f,1.0f})assert(hasVertex(hangar,{-3268,18.5f,-1024+sign*27.7f}));
        const Chunk dock=chunkAt(20,5,lod);
        assert(hasVertex(dock,{2668.38f,4.5f,755.38f}));
        const Chunk launch=chunkAt(24,8,lod);
        assert(hasVertex(launch,{3089.93f,World::WaterLevel+1.16f,1084.5f}));
        assert(hasVertex(launch,{3091.32f,World::WaterLevel+2.67f,1081.13f}));
    }
    std::puts("Landmarks: exchange spire, clock pavilion, market roofs, hangar, dock and clinic launch retain defining geometry at every LOD");
}
void vegetationAnchors() {
    constexpr std::array<std::pair<int,int>,12> locations{{
        {-33,15},{-32,16},{8,-22},{9,-22},{8,-21},{9,-21},{8,-23},{9,-23},{31,-2},{32,-2},{19,5},{20,6}
    }};
    World world;std::array<bool,4> kinds{};size_t matched=0;
    for(const auto& [x,z]:locations) {
        const std::vector<TreeSpec> trees=describeNaturalTrees(world,x,z),again=describeNaturalTrees(world,x,z);
        assert(trees.size()==again.size());
        const Chunk detail=chunkAt(x,z,WorldLod::Detail);
        std::array<Chunk,2> coarse{chunkAt(x,z,WorldLod::Medium),chunkAt(x,z,WorldLod::Far)};
        std::map<Point,bool> anchors;
        for(size_t i=0;i<trees.size();++i) {
            const TreeSpec& tree=trees[i];const TreeSpec& repeated=again[i];
            assert(same(tree.position,repeated.position)&&tree.scale==repeated.scale&&tree.seed==repeated.seed&&tree.kind==repeated.kind);
            assert(tree.scale>0&&std::isfinite(tree.scale));assert(anchors.emplace(point(tree.position),true).second);
            kinds[static_cast<size_t>(tree.kind)]=true;
            bool collisionMatch=false;
            for(const Box& box:detail.solids)if(close((box.min.x+box.max.x)*.5f,tree.position.x)&&
                close((box.min.z+box.max.z)*.5f,tree.position.z)&&close(box.min.y,tree.position.y))collisionMatch=true;
            assert(collisionMatch);
            for(const Chunk& chunk:coarse) {
                std::map<Point,bool> roots;
                for(const Vertex& vertex:chunk.mesh.vertices) {
                    const Vec3 delta=vertex.position-tree.position;
                    if(std::abs(vertex.normal.y)<.00001f&&std::abs(delta.y)<.003f&&
                       delta.x*delta.x+delta.z*delta.z<.30f*.30f*tree.scale*tree.scale)
                        roots.emplace(point(vertex.position),true);
                }
                assert(roots.size()>=3);
                Vec3 center{};
                for(const auto& [root,present]:roots){(void)present;center+=Vec3{root[0],root[1],root[2]};}
                center=center/static_cast<float>(roots.size());
                if(!close(center.x,tree.position.x,.003f)||!close(center.y,tree.position.y,.003f)||!close(center.z,tree.position.z,.003f))
                    std::fprintf(stderr,"Tree anchor mismatch chunk=(%d,%d), tree=%zu, roots=%zu, delta=(%.6f,%.6f,%.6f)\n",x,z,i,roots.size(),center.x-tree.position.x,center.y-tree.position.y,center.z-tree.position.z);
                assert(close(center.x,tree.position.x,.003f)&&close(center.y,tree.position.y,.003f)&&close(center.z,tree.position.z,.003f));
                ++matched;
            }
        }
    }
    for(bool present:kinds)assert(present);
    std::printf("Vegetation: %zu coarse trunk anchors match deterministic descriptors and detail collision across all four tree kinds\n",matched);
}
bool terrainHeightAt(const Mesh& terrain,double x,double z,double& height) {
    for(size_t i=0;i<terrain.indices.size();i+=3) {
        const Vec3 a=terrain.vertices[terrain.indices[i]].position;
        const Vec3 b=terrain.vertices[terrain.indices[i+1]].position;
        const Vec3 c=terrain.vertices[terrain.indices[i+2]].position;
        const double bx=double(b.x)-a.x,bz=double(b.z)-a.z,cx=double(c.x)-a.x,cz=double(c.z)-a.z;
        const double determinant=bx*cz-bz*cx;
        assert(determinant!=0);
        const double dx=x-a.x,dz=z-a.z;
        const double u=(dx*cz-dz*cx)/determinant,v=(bx*dz-bz*dx)/determinant;
        if(u>=-1e-5&&v>=-1e-5&&u+v<=1+1e-5) {
            height=double(a.y)+u*(double(b.y)-a.y)+v*(double(c.y)-a.y);return true;
        }
    }
    return false;
}
void coarseRoadVisibility() {
    const World world;std::map<std::pair<int,int>,bool> locations;
    for(const auto& location:std::array<std::pair<int,int>,7>{{{-26,-8},{-25,-8},{-33,15},{-32,16},{8,-22},{19,5},{20,6}}})
        locations.emplace(location,false);
    for(int z=-11;z<=-5;++z)for(int x=-27;x<=-23;++x)locations.emplace(std::make_pair(x,z),false);
    for(int z=12;z<=20;z+=2)for(int x=-36;x<=-28;x+=2)locations.emplace(std::make_pair(x,z),false);
    for(int z=-26;z<=-18;z+=2)for(int x=6;x<=12;x+=2)locations.emplace(std::make_pair(x,z),false);
    for(int z=-1;z<=0;++z)for(int x=18;x<=35;++x)locations[std::make_pair(x,z)]=true;
    size_t checked=0;double minimum=std::numeric_limits<double>::infinity();
    for(const auto& [location,checkDetail]:locations)for(WorldLod lod:Lods) {
        if(lod==WorldLod::Detail&&!checkDetail)continue;
        const auto [x,z]=location;
        const Chunk chunk=chunkAt(x,z,lod);Mesh terrain;appendTerrain(terrain,world,x,z,lod);
        if(checkDetail)for(const Vertex& vertex:terrain.vertices) {
            const Vec3 p=vertex.position;assert(world.height(p.x,p.z)>=p.y-.0001f);
        }
        for(size_t i=0;i<chunk.mesh.indices.size();i+=3) {
            const Vertex& a=chunk.mesh.vertices[chunk.mesh.indices[i]];
            const Vertex& b=chunk.mesh.vertices[chunk.mesh.indices[i+1]];
            const Vertex& c=chunk.mesh.vertices[chunk.mesh.indices[i+2]];
            if(a.material!=4)continue;
            assert(b.material==4&&c.material==4);
            const double px=(double(a.position.x)+b.position.x+c.position.x)/3;
            const double pz=(double(a.position.z)+b.position.z+c.position.z)/3;
            const double roadHeight=(double(a.position.y)+b.position.y+c.position.y)/3;
            double terrainHeight=0;const bool found=terrainHeightAt(terrain,px,pz,terrainHeight);
            if(!found)std::fprintf(stderr,"Coarse road outside terrain: chunk=(%d,%d), LOD=%u, triangle=%zu, position=(%.6f,%.6f)\n",x,z,unsigned(lod),i/3,px,pz);
            assert(found);
            const double clearance=roadHeight-terrainHeight;
            if(clearance<.015-1e-4)std::fprintf(stderr,"Buried coarse road: chunk=(%d,%d), LOD=%u, triangle=%zu, position=(%.4f,%.4f), clearance=%.6f\n",x,z,unsigned(lod),i/3,px,pz,clearance);
            assert(clearance>=.015-1e-4);minimum=std::min(minimum,clearance);++checked;
        }
    }
    assert(checked>0);
    assert(close(world.height(3200,0),5.2f)&&close(world.height(3200,10),5.2f));
    for(float endpoint:{2320.0f,4540.0f}) {
        const float before=world.height(endpoint-.001f,0),at=world.height(endpoint,0),after=world.height(endpoint+.001f,0);
        assert(close(before,at,.01f)&&close(after,at,.01f));
    }
    bool landmarkFound=false;
    for(const Landmark& landmark:World::landmarks())if(std::string(landmark.name)=="Glasswater Causeway") {
        assert(landmark.position.x==4096&&landmark.position.z==0);
        assert(close(landmark.position.y,world.height(landmark.position.x,landmark.position.z)));landmarkFound=true;
    }
    assert(landmarkFound);
    std::printf("Road visibility: %zu triangle centroids across %zu regional chunks retain at least %.6f m terrain clearance\n",checked,locations.size(),minimum);
}
void completeCacheBudget() {
    constexpr std::array<std::pair<int,int>,10> centers{{{0,0},{-4,4},{5,5},{-8,-5},{20,0},{-32,15},{8,-22},{20,6},{32,-2},{-32,16}}};
    bool allWithinBudget=true;
    for(const auto& [cx,cz]:centers) {
        size_t bytes=0,triangles=0,maximum=0,tiles=0;
        std::array<std::array<size_t,7>,2> biomeBytes{},biomeTiles{};
        const World world;
        // Both layers stay resident beneath finer tiles so shifts always have coverage.
        for(WorldLod lod:{WorldLod::Medium,WorldLod::Far}) {
            const int radius=lod==WorldLod::Medium?World::MediumRadius:World::PrefetchRadius;
            for(int z=-radius;z<=radius;++z)for(int x=-radius;x<=radius;++x) {
                const Chunk chunk=chunkAt(cx+x,cz+z,lod);const size_t size=World::chunkBytes(chunk);
                validateTriangleAreas(chunk);
                assert(chunk.solids.empty()&&chunk.lights.empty());assert(size<=World::MaxVisualChunkBytes);
                bytes+=size;maximum=std::max(maximum,size);triangles+=chunk.mesh.indices.size()/3;++tiles;
                const size_t layer=lod==WorldLod::Medium?0:1;
                const size_t biome=static_cast<size_t>(world.biome((cx+x)*World::ChunkSize+64,(cz+z)*World::ChunkSize+64));
                biomeBytes[layer][biome]+=size;++biomeTiles[layer][biome];
            }
        }
        allWithinBudget=allWithinBudget&&bytes<=World::MaxVisualCacheBytes;
        std::printf("Prefetch cache (%d,%d): %zu visual tiles, %zu triangles, %zu capacity bytes; largest tile %zu bytes\n",cx,cz,tiles,triangles,bytes,maximum);
        if(cx==0&&cz==0) {
            constexpr std::array<const char*,7> names{"Downtown","Residential","Countryside","Wetland","Beach","Island","Ocean"};
            for(size_t i=0;i<names.size();++i)std::printf("  %s: Medium %zuB/%zu tiles, Far %zuB/%zu tiles\n",names[i],biomeBytes[0][i],biomeTiles[0][i],biomeBytes[1][i],biomeTiles[1][i]);
        }
        std::fflush(stdout);
    }
    assert(allWithinBudget);
}
}
int main() {
    terrainSeams();chunkDeterminism();skylineShapes();landmarkShapes();vegetationAnchors();coarseRoadVisibility();completeCacheBudget();
    std::puts("World LOD geometry tests passed");
}
