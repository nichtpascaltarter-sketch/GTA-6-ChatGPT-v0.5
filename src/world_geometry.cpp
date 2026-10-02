#include "world_geometry.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mc {
namespace {
constexpr float WaterLevel=World::WaterLevel;
constexpr float RoadHalf=10.0f;
struct GarageGrade {float x0,x1,z0,z1;};
GarageGrade garageGrade() {
    const auto site=World::garageSite();return {site.shell.min.x-8,site.shell.max.x+6,site.shell.min.z-4,site.shell.max.z+20};
}
bool garagePaving(float x,float z) {
    const auto grade=garageGrade();return x>=grade.x0&&x<=grade.x1&&z>=grade.z0&&z<=grade.z1;
}
float garageElevation(float x,float z) {
    const auto site=World::garageSite();const auto grade=garageGrade();
    return site.floorHeight*clamp(std::min({(x-grade.x0)/8,(grade.x1-x)/6,(z-grade.z0)/4,(grade.z1-z)/20}),0,1);
}
struct SurfaceArea {
    float x0,z0,x1,z1;
    bool contains(float x,float z) const {return x>=x0&&x<=x1&&z>=z0&&z<=z1;}
};
constexpr SurfaceArea AirfieldPaving[]={{-3218,-1256,-3182,-744},{-3288,-1058,-3182,-994},{-3182,-1029,-3072,-1019}};
constexpr SurfaceArea DockApproach{2504,765,2600,771};
struct HangarSpec {Vec3 origin,half;float roofRise;};
constexpr HangarSpec AirfieldHangar{{-3268,4,-1024},{20,5,27},4.5f};
struct LaunchSpec {Vec3 origin;std::array<float,6> stations,widths;};
constexpr LaunchSpec ClinicLaunch{{3090,World::WaterLevel,1080},{-4.25f,-3.0f,-.8f,1.3f,3.25f,4.5f},{1.27f,1.62f,1.62f,1.48f,.95f,.07f}};
float smooth(float a,float b,float x) { float t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t); }
uint32_t seedAt(int x,int z,uint32_t salt=0) { return hash32(uint32_t(x)*0x9e3779b9u ^ uint32_t(z)*0x85ebca6bu ^ salt); }
float coast(float z) { return 2480+180*std::sin(z*.0009f)+95*std::sin(z*.0022f); }
float mainlandDistance(float x,float z) {
    float south=-4540+180*std::sin(x*.0011f);
    return std::min(std::min(coast(z)-x,z-south),std::min(World::Extent-std::abs(x),World::Extent-z));
}
float islandDistance(float x,float z) {
    struct Isle { float x,z,rx,rz; };
    constexpr Isle islands[]={{4140,-250,620,850},{4740,1720,620,470},{3860,-3020,490,730},{5340,-2210,310,410},{4150,3880,530,360}};
    float d=-100000;
    for(const auto& i:islands) {
        float dx=(x-i.x)/i.rx,dz=(z-i.z)/i.rz;
        float angle=std::atan2(dz,dx);
        float rim=1+.055f*std::sin(angle*5)+.035f*std::cos(angle*9);
        d=std::max(d,(rim-std::sqrt(dx*dx+dz*dz))*std::min(i.rx,i.rz));
    }
    return d;
}
float landDistance(float x,float z) { return std::max(mainlandDistance(x,z),islandDistance(x,z)); }
bool wetlands(float x,float z) { return x>400 && x<2100 && z<-1750 && z>-4080; }
bool airfieldReserve(float x,float z) {
    return (std::abs(x+3200)<60 && std::abs(z+1000)<420) || (x>-3300&&x<-3240&&z>-1064&&z<-984);
}
bool dockDeck(float x,float z) {
    return (x>=2600&&x<=2675&&std::abs(z-768)<=3)||(x>=2667&&x<=2675&&z>=754&&z<=782);
}
bool siteReserve(float x,float z) {return airfieldReserve(x,z)||(x>2494&&x<2690&&z>742&&z<794);}
float naturalHeight(float x,float z) {
    float md=mainlandDistance(x,z),id=islandDistance(x,z),d=std::max(md,id);
    if(d<0) return std::max(-35.0f,-2.8f+d*.055f);
    float shore=smooth(0,100,d);
    if(id>md) {
        float hills=3+26*smooth(70,430,d)*(.65f+.35f*std::sin(x*.007f+z*.005f));
        return lerp(-2.8f,hills,shore);
    }
    float west=smooth(1900,5300,-x),north=smooth(2100,5100,z);
    float ripple=std::sin(x*.0015f)*std::cos(z*.0012f)*.5f+.5f;
    float rolling=(west+north)*(.65f+.35f*std::sin(x*.0007f+z*.001f))*(18+85*ripple);
    float h=rolling;
    if(wetlands(x,z)) {
        float w=smooth(400,680,x)*smooth(2100,1810,x)*smooth(-1750,-2040,z)*smooth(-4080,-3790,z);
        h=lerp(h,-1.15f+.9f*std::sin(x*.015f)*std::cos(z*.009f),w);
    }
    float ground=lerp(-2.8f,h,shore);
    float airport=smooth(120,40,std::max(-3200-x-55,x+3200))*smooth(440,340,std::abs(z+1000));
    return lerp(ground,4.0f,airport);
}
float gridDistance(float p,float step) { return std::abs(p-std::round(p/step)*step); }
float localRoadStep(float x,float z) {
    if(x>=-1664 && x<=1536 && z>=-1792 && z<=2048) return 128;
    if(x>=-3072 && x<=2048 && z>=-2560 && z<=3072) return 256;
    return 512;
}
bool causeway(float x,float z) { return x>coast(z)-160 && x<4540 && std::abs(z)<=RoadHalf+.5f; }
bool axialRoad(float x,float z) {
    if(std::abs(x+3200)<40&&std::abs(z+1000)<300)return false;
    if(mainlandDistance(x,z)<45 || std::abs(x)>World::Extent-30 || std::abs(z)>World::Extent-30) return false;
    float s=localRoadStep(x,z);
    return gridDistance(x,s)<RoadHalf || gridDistance(z,s)<RoadHalf;
}
bool coastalRoad(float x,float z) { return std::abs(x-(coast(z)-185))<8.0f && z>-4210 && z<5500; }
bool plazaBlock(int x,int z) {return (x==3&&z==3)||(x==-4&&z==2)||seedAt(x,z)%17==0;}
SurfaceArea residentialPath(float px,float pz,uint32_t seed) {
    const float halfDepth=11.0f+random01(seed+1)*6;
    return {px-2,pz-halfDepth-10,px+2,pz-halfDepth};
}
Vec3 rotated(Vec3 p,float yaw) {float s=std::sin(yaw),c=std::cos(yaw);return {p.x*c+p.z*s,p.y,-p.x*s+p.z*c};}
void tri(Mesh& m,Vec3 a,Vec3 b,Vec3 c,Vec3 color,float material=0) {
    uint32_t n=static_cast<uint32_t>(m.vertices.size()); Vec3 normal=normalized(cross(b-a,c-a));
    m.vertices.push_back({a,normal,color,material});m.vertices.push_back({b,normal,color,material});m.vertices.push_back({c,normal,color,material});
    m.indices.insert(m.indices.end(),{n,n+1,n+2});
}
Vec3 terrainNormal(float x,float z) {
    constexpr float step=.5f;
    return normalized(Vec3{naturalHeight(x-step,z)-naturalHeight(x+step,z),2*step,naturalHeight(x,z-step)-naturalHeight(x,z+step)});
}
void groundPatch(Mesh& m,const World& world,float x0,float z0,float x1,float z1,Vec3 color,float lift=0,float material=0,bool natural=false,size_t terrainIndices=0) {
    if(terrainIndices) {
        // Clip the paving footprint to the terrain triangulation. Raising each clipped
        // vertex onto the road profile keeps the entire paved triangle above the land,
        // including coarse cells and the island end of the causeway.
        for(size_t index=0;index<terrainIndices;index+=3) {
            std::array<Vec3,8> polygon{},clipped{};
            for(size_t j=0;j<3;++j)polygon[j]=m.vertices[m.indices[index+j]].position;
            const float minX=std::min({polygon[0].x,polygon[1].x,polygon[2].x}),maxX=std::max({polygon[0].x,polygon[1].x,polygon[2].x});
            const float minZ=std::min({polygon[0].z,polygon[1].z,polygon[2].z}),maxZ=std::max({polygon[0].z,polygon[1].z,polygon[2].z});
            if(maxX<=x0||minX>=x1||maxZ<=z0||minZ>=z1)continue;
            size_t count=3;
            for(int plane=0;plane<4&&count;++plane) {
                const bool vertical=plane<2,lower=plane%2==0;
                const float edge=vertical?(lower?x0:x1):(lower?z0:z1);
                auto coordinate=[&](Vec3 p){return vertical?p.x:p.z;};
                auto inside=[&](Vec3 p){return lower?coordinate(p)>=edge:coordinate(p)<=edge;};
                size_t nextCount=0;Vec3 previous=polygon[count-1];bool previousInside=inside(previous);
                for(size_t j=0;j<count;++j) {
                    const Vec3 current=polygon[j];const bool currentInside=inside(current);
                    if(currentInside!=previousInside) {
                        const float t=(edge-coordinate(previous))/(coordinate(current)-coordinate(previous));
                        Vec3 intersection=lerp(previous,current,t);
                        if(vertical)intersection.x=edge;else intersection.z=edge;
                        clipped[nextCount++]=intersection;
                    }
                    if(currentInside)clipped[nextCount++]=current;
                    previous=current;previousInside=currentInside;
                }
                polygon=clipped;count=nextCount;
            }
            for(size_t j=0;j<count;++j) {
                auto& vertex=polygon[j];
                if(causeway(vertex.x,vertex.z))vertex.y=std::max(vertex.y,world.height(vertex.x,vertex.z));
                vertex.y+=lift;
            }
            for(size_t j=1;j+1<count;++j) {
                const Vec3 area=cross(polygon[j]-polygon[0],polygon[j+1]-polygon[0]);
                if(dot(area,area)<1e-10f)continue;
                tri(m,polygon[0],polygon[j],polygon[j+1],color,material);
            }
        }
        return;
    }
    auto p=[&](float x,float z){return Vec3{x,(natural?naturalHeight(x,z):world.height(x,z))+lift,z};};
    addQuad(m,p(x0,z0),p(x0,z1),p(x1,z1),p(x1,z0),color,material);
    if(natural)for(size_t i=m.vertices.size()-4;i<m.vertices.size();++i) {
        const Vec3 v=m.vertices[i].position;m.vertices[i].normal=terrainNormal(v.x,v.z);
    }
}
void sidewalkPatch(Mesh& mesh,const World& world,float x0,float z0,float x1,float z1,Vec3 color,float lift) {
    const auto grade=garageGrade();
    if(x1<=grade.x0||x0>=grade.x1||z1<=grade.z0||z0>=grade.z1) {groundPatch(mesh,world,x0,z0,x1,z1,color,lift);return;}
    auto patch=[&](float ax,float az,float bx,float bz){if(bx>ax&&bz>az)groundPatch(mesh,world,ax,az,bx,bz,color,lift);};
    patch(x0,z0,std::min(x1,grade.x0),z1);patch(std::max(x0,grade.x1),z0,x1,z1);
    const float a=std::max(x0,grade.x0),b=std::min(x1,grade.x1);
    patch(a,z0,b,std::min(z1,grade.z0));patch(a,std::max(z0,grade.z1),b,z1);
}

void cone(Mesh& m,Vec3 bottom,float radius,float height,Vec3 color,int sides) {
    for(int i=0;i<sides;++i) {
        float a=2*Pi*float(i)/float(sides),b=2*Pi*float(i+1)/float(sides);
        tri(m,bottom+Vec3{radius*std::cos(b),0,radius*std::sin(b)},bottom+Vec3{radius*std::cos(a),0,radius*std::sin(a)},bottom+Vec3{0,height,0},color);
    }
}
void branch(Mesh& m,Vec3 a,Vec3 b,float radius0,float radius1,Vec3 color,int sides=5) {
    Vec3 direction=normalized(b-a),u=normalized(cross(direction,std::abs(direction.y)>.95f?Vec3{1,0,0}:Vec3{0,1,0})),v=cross(direction,u);
    for(int i=0;i<sides;++i) {
        float t0=2*Pi*i/sides,t1=2*Pi*(i+1)/sides;
        Vec3 r0=u*std::cos(t0)+v*std::sin(t0),r1=u*std::cos(t1)+v*std::sin(t1);
        addQuad(m,a+r0*radius0,a+r1*radius0,b+r1*radius1,b+r0*radius1,color);
    }
}
void leafCrown(Mesh& m,Vec3 p,Vec3 radius,Vec3 color,uint32_t seed,int sides=7) {
    // Three unequal crown rings keep individual branches readable without texture cards.
    auto point=[&](int i,float y,float width) {
        float a=2*Pi*i/sides,irregular=.86f+random01(seed+static_cast<uint32_t>((i%sides)*13))*.27f;
        return p+Vec3{std::cos(a)*radius.x*width*irregular,y*radius.y,std::sin(a)*radius.z*width*irregular};
    };
    for(int i=0;i<sides;++i) {
        Vec3 a=point(i,-.35f,.83f),b=point(i+1,-.35f,.83f),c=point(i,.4f,.88f),d=point(i+1,.4f,.88f);
        Vec3 shade=color*(.84f+random01(seed+static_cast<uint32_t>(i)*71)*.28f);
        tri(m,p+Vec3{0,-radius.y,0},a,b,shade*.78f);
        addQuad(m,b,a,c,d,shade);
        tri(m,d,c,p+Vec3{radius.x*.12f,radius.y,0},shade*1.1f);
    }
}
void grassTuft(Mesh& m,Vec3 p,float scale,uint32_t seed,bool reed) {
    int blades=reed?5:4;
    for(int i=0;i<blades;++i) {
        float angle=random01(seed+static_cast<uint32_t>(i)*13)*2*Pi;
        Vec3 dir{std::cos(angle),0,std::sin(angle)},side{-dir.z,0,dir.x};
        float height=(reed?1.6f:.45f)*scale*(.65f+random01(seed+static_cast<uint32_t>(i)*17)*.65f),width=(reed?.055f:.045f)*scale;
        Vec3 root=p+dir*(.15f*scale),bend=root+Vec3{0,height*.63f,0}+dir*(height*.11f),tip=root+Vec3{0,height,0}+dir*(height*.35f);
        Vec3 color=reed?Vec3{.35f,.43f,.17f}:Vec3{.28f,.39f,.12f};
        addQuad(m,root-side*width,root+side*width,bend+side*width*.7f,bend-side*width*.7f,color);
        tri(m,bend-side*width*.7f,bend+side*width*.7f,tip,color*1.1f);
        if(reed && i%2==0)branch(m,tip-Vec3{0,.21f*scale,0},tip+Vec3{0,.18f*scale,0},.045f*scale,.035f*scale,{.35f,.24f,.11f},3);
    }
}
void palm(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    float h=(8+random01(seed)*3)*scale,leanAngle=random01(seed+41)*2*Pi;
    Vec3 lean{std::cos(leanAngle)*scale*.65f,0,std::sin(leanAngle)*scale*.65f};
    Vec3 middle=p+Vec3{0,h*.53f,0}+lean*.3f,top=p+Vec3{0,h,0}+lean;
    branch(m,p,middle,.25f*scale,.20f*scale,{.41f,.31f,.20f},5);
    branch(m,middle,top,.20f*scale,.15f*scale,{.48f,.39f,.25f},5);
    cone(m,top-Vec3{0,.35f*scale,0},.38f*scale,.65f*scale,{.27f,.34f,.11f},6);
    for(int f=0;f<8;++f) {
        float a=2*Pi*f/8+random01(seed+17)*2;
        Vec3 dir{std::cos(a),0,std::sin(a)},side{-dir.z,0,dir.x};
        float span=(3.9f+random01(seed+static_cast<uint32_t>(f)*37)*.9f)*scale;
        Vec3 bend=top+dir*(span*.48f)+Vec3{0,1.0f*scale,0},tip=top+dir*span-Vec3{0,1.65f*scale,0};
        Vec3 color{.105f+.007f*f,.30f+.008f*f,.075f};
        addQuad(m,top-side*.06f*scale,bend-side*.25f*scale,bend+side*.25f*scale,top+side*.06f*scale,color);
        addQuad(m,bend-side*.25f*scale,tip-side*.025f*scale,tip+side*.025f*scale,bend+side*.25f*scale,color*.91f);
        for(int pair=0;pair<3;++pair) {
            float t=.26f+pair*.22f;
            Vec3 rib=t<.48f?lerp(top,bend,t/.48f):lerp(bend,tip,(t-.48f)/.52f);
            float width=(.67f-.10f*pair)*scale;
            for(float sign:{-1.0f,1.0f})tri(m,rib-dir*.30f*scale,rib+side*(width*sign)+dir*.43f*scale-Vec3{0,.28f*scale,0},rib+dir*.49f*scale,color*(sign>0?1.05f:.89f));
        }
    }
}
void broadleaf(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    Vec3 fork=p+Vec3{.18f*scale,3.6f*scale,-.12f*scale};
    branch(m,p,fork,.40f*scale,.20f*scale,{.28f,.23f,.16f});
    for(int limb=0;limb<5;++limb) {
        float a=2*Pi*limb/5+random01(seed)*2,spread=(1.7f+random01(seed+limb*19)*1.1f)*scale;
        Vec3 end=p+Vec3{std::cos(a)*spread,(4.9f+random01(seed+limb*41)*1.5f)*scale,std::sin(a)*spread};
        branch(m,fork-Vec3{0,.6f*scale,0},end,.17f*scale,.055f*scale,{.31f,.25f,.17f},4);
        leafCrown(m,end,{2.3f*scale,1.9f*scale,2.2f*scale},{.14f,.31f,.09f},seed+limb*83);
    }
}
void cypress(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    branch(m,p,p+Vec3{.12f*scale,8.7f*scale,0},.52f*scale,.08f*scale,{.38f,.31f,.23f},6);
    for(int root=0;root<5;++root) {
        float a=2*Pi*root/5;Vec3 dir{std::cos(a),0,std::sin(a)};
        branch(m,p+dir*(1.5f*scale),p+Vec3{0,1.7f*scale,0},.17f*scale,.21f*scale,{.36f,.29f,.21f},4);
        if(root%2==0)branch(m,p+dir*(2.2f*scale),p+dir*(2.3f*scale)+Vec3{0,.7f*scale,0},.18f*scale,.065f*scale,{.35f,.28f,.20f},4);
    }
    for(int level=0;level<4;++level) {
        float y=(4.7f+level*1.7f)*scale,spread=(2.65f-level*.42f)*scale;
        Vec3 crown=p+Vec3{std::sin(float(level)*2.3f)*.35f*scale,y,0};
        leafCrown(m,crown,{spread,1.65f*scale,spread*.86f},{.15f,.29f,.16f},seed+level*61,7);
        if(level<3)for(float sign:{-1.0f,1.0f}) {
            Vec3 b=crown+Vec3{spread*.8f*sign,-.25f*scale,0};
            branch(m,p+Vec3{0,y-.7f*scale,0},b,.10f*scale,.035f*scale,{.34f,.29f,.23f},4);
            addQuad(m,b+Vec3{-.10f*scale,0,0},b+Vec3{-.035f*scale,-1.75f*scale,0},b+Vec3{.04f*scale,-1.6f*scale,0},b+Vec3{.13f*scale,0,0},{.40f,.43f,.30f});
        }
    }
}
void mangrove(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    Vec3 fork=p+Vec3{0,2.1f*scale,0};
    branch(m,p,fork,.24f*scale,.16f*scale,{.33f,.30f,.22f},5);
    for(int limb=0;limb<5;++limb) {
        float a=2*Pi*limb/5+random01(seed)*2;Vec3 dir{std::cos(a),0,std::sin(a)};
        Vec3 elbow=p+dir*(1.45f*scale)+Vec3{0,1.35f*scale,0};
        branch(m,p+dir*(2.4f*scale),elbow,.12f*scale,.09f*scale,{.35f,.30f,.23f},4);
        branch(m,elbow,fork,.09f*scale,.12f*scale,{.35f,.30f,.23f},4);
        Vec3 end=fork+dir*(2.3f*scale)+Vec3{0,(.9f+random01(seed+limb*31))*scale,0};
        branch(m,fork,end,.13f*scale,.04f*scale,{.32f,.29f,.21f},4);
        leafCrown(m,end,{1.9f*scale,1.35f*scale,1.85f*scale},{.17f,.34f,.13f},seed+limb*97,6);
    }
}
void streetlight(Chunk& chunk,Vec3 p,float yaw) {
    Mesh& m=chunk.mesh;
    addCylinder(m,p,.12f,8.4f,{.20f,.23f,.25f},6,1);
    addBox(m,p+rotated({1.0f,8.3f,0},yaw),{1.1f,.08f,.08f},{.22f,.25f,.27f},yaw,1);
    addBox(m,p+rotated({1.8f,8.24f,0},yaw),{.48f,.07f,.25f},{.94f,.85f,.60f},yaw,2);
    // The light originates on the underside of the visible lens, directly above the road edge.
    chunk.lights.push_back({p+rotated({1.8f,8.17f,0},yaw),28,{1,.72f,.40f},100,{0,-1,0},-.15f});
    chunk.solids.push_back({p-Vec3{.12f,0,.12f},p+Vec3{.12f,8.4f,.12f}});
}
void roofGable(Mesh& m,Vec3 p,float hx,float hz,float rise,Vec3 color) {
    Vec3 a=p+Vec3{-hx,0,-hz},b=p+Vec3{-hx,0,hz},c=p+Vec3{hx,0,hz},d=p+Vec3{hx,0,-hz},e=p+Vec3{0,rise,-hz},f=p+Vec3{0,rise,hz};
    addQuad(m,a,b,f,e,color);addQuad(m,e,f,c,d,color);tri(m,a,e,d,color);tri(m,c,f,b,color);
}
void windows(Mesh& mesh,Vec3 p,float hx,float hz,float height,uint32_t seed,bool tower) {
    int floors=std::max(1,std::min(24,int((height-2)/3.4f)));
    int nx=std::max(2,std::min(6,int(hx*2/5))),nz=std::max(2,std::min(6,int(hz*2/5)));
    float pitch=(height-1.8f)/floors;
    for(int face=0;face<4;++face) {
        int count=face<2?nx:nz;
        float half=face<2?hx:hz;
        for(int row=0;row<floors;++row) for(int col=0;col<count;++col) {
            float u=-half+(col+.5f)*half*2/count;
            float y=p.y+1.6f+row*pitch,wh=std::min(1.7f,pitch*.62f),ww=half*2/count*(tower?.76f:.49f);
            float brightness=.8f+random01(seed+uint32_t(row*31+col*7+face*127))*.3f;
            Vec3 glass=Vec3{.22f,.36f,.40f}*brightness;
            if(random01(seed+uint32_t(row*173+col*31+face))>.78f) glass={.46f,.41f,.28f};
            if(face<2) {
                float z=p.z+(face==0?hz+.026f:-hz-.026f);
                if(face==0) addQuad(mesh,{p.x+u-ww*.5f,y,z},{p.x+u+ww*.5f,y,z},{p.x+u+ww*.5f,y+wh,z},{p.x+u-ww*.5f,y+wh,z},glass,2);
                else addQuad(mesh,{p.x+u+ww*.5f,y,z},{p.x+u-ww*.5f,y,z},{p.x+u-ww*.5f,y+wh,z},{p.x+u+ww*.5f,y+wh,z},glass,2);
            } else {
                float x=p.x+(face==2?hx+.026f:-hx-.026f);
                if(face==2) addQuad(mesh,{x,y,p.z+u+ww*.5f},{x,y,p.z+u-ww*.5f},{x,y+wh,p.z+u-ww*.5f},{x,y+wh,p.z+u+ww*.5f},glass,2);
                else addQuad(mesh,{x,y,p.z+u-ww*.5f},{x,y,p.z+u+ww*.5f},{x,y+wh,p.z+u+ww*.5f},{x,y+wh,p.z+u-ww*.5f},glass,2);
            }
        }
    }
}
void facadeLine(Mesh& m,Vec3 origin,Vec3 along,float x0,float y0,float x1,float y1,float width,Vec3 color,float material=0) {
    Vec3 a=origin+along*x0+Vec3{0,y0,0},b=origin+along*x1+Vec3{0,y1,0};
    Vec3 side=normalized(along*-(y1-y0)+Vec3{0,x1-x0,0})*(width*.5f);
    addQuad(m,a+side,b+side,b-side,a-side,color,material);
}
const char* letterStrokes(char c) {
    switch(c) {
    case 'A':return "abcefg";case 'B':return "abcdefg";case 'C':return "adef";case 'D':return "abcdef";
    case 'E':return "adefg";case 'F':return "aefg";case 'G':return "acdefg";case 'H':return "bcefg";
    case 'I':return "adlm";case 'J':return "bcde";case 'K':return "efik";case 'L':return "def";
    case 'M':return "bcefhi";case 'N':return "bcefhk";case 'O':return "abcdef";case 'P':return "abefg";
    case 'Q':return "abcdefk";case 'R':return "abefgk";case 'S':return "acdfg";case 'T':return "alm";
    case 'U':return "bcdef";case 'V':return "bfjk";case 'W':return "bcefjk";case 'X':return "hijk";
    case 'Y':return "him";case 'Z':return "adij";default:return "";
    }
}
void signText(Mesh& m,Vec3 center,Vec3 along,const char* label,float scale,Vec3 color,float material=2) {
    // Thirteen original monoline strokes form a compact, geometry-only shop alphabet.
    static constexpr float strokes[13][4]={{0,6,4,6},{4,6,4,3},{4,3,4,0},{4,0,0,0},{0,0,0,3},{0,3,0,6},{0,3,4,3},{0,6,2,3},{4,6,2,3},{0,0,2,3},{4,0,2,3},{2,6,2,3},{2,3,2,0}};
    int count=0;for(const char* p=label;*p;++p)++count;
    Vec3 origin=center-along*((count*5.3f-1.3f)*scale*.5f);
    for(const char* p=label;*p;++p,origin+=along*(5.3f*scale))for(const char* s=letterStrokes(*p);*s;++s) {
        const float* a=strokes[*s-'a'];facadeLine(m,origin,along,a[0]*scale,a[1]*scale,a[2]*scale,a[3]*scale,.31f*scale,color,material);
    }
}
struct ShopfrontSpec {
    Vec3 origin;float width=0,yaw=0;uint32_t seed=0;bool illuminated=false,residential=false;
    Vec3 outward() const {return rotated({0,0,-1},yaw);}
    Vec3 door() const {return origin+rotated({width*.65f,0,-.3f},yaw);}
};
ShopfrontSpec buildingShopfront(const worldGeometry::BuildingSpec& building,bool west) {
    const auto p=building.position;const float hx=building.halfWidth,hz=building.halfDepth;
    const int cx=int(std::floor(p.x/128)),cz=int(std::floor(p.z/128));
    const bool home=west&&std::abs(cx)<=1&&std::abs(cz)<=1&&p.z-cz*128<64;
    return {p+(west?Vec3{-hx-.04f,0,0}:Vec3{0,0,-hz-.04f}),
        (west?hz:hx)*.86f,west?Pi*.5f:0,building.seed+(west?3u:0u),
        building.seed%(west?5u:3u)==0,home};
}
void shopfront(Chunk& chunk,const ShopfrontSpec& spec) {
    const Vec3 p=spec.origin;const float width=spec.width,yaw=spec.yaw;
    const uint32_t seed=spec.seed;const bool illuminated=spec.illuminated;
    Mesh& m=chunk.mesh;
    static const char* names[]={"LOW TIDE","CITRUS","GULL CAFE","ORBIT VINYL","SUN MART","RELAY","MESA DELI","TIDAL TEA"};
    Vec3 along=rotated({1,0,0},yaw),out=rotated({0,0,-1},yaw);
    Vec3 awning=seed%3==0?Vec3{.08f,.32f,.30f}:seed%3==1?Vec3{.60f,.23f,.12f}:Vec3{.23f,.30f,.44f};
    addBox(m,p+Vec3{0,2.0f,0}+out*.08f,{width,1.8f,.10f},{.13f,.18f,.19f},yaw);
    for(int k=-2;k<=2;++k) {
        Vec3 q=p+along*(k*width*.37f)+Vec3{0,1.7f,0}+out*.20f;
        Vec3 across=along*(width*.16f),up{0,1.4f,0};
        addQuad(m,q-across-up,q-across+up,q+across+up,q+across-up,{.13f,.25f,.27f},2);
        addBox(m,q+along*(width*.18f),{.045f,1.6f,.10f},{.60f,.63f,.56f},yaw,1);
    }
    addBox(m,p+Vec3{0,3.92f,0}+out*.22f,{width,.61f,.12f},awning,yaw);
    signText(m,p+Vec3{0,3.46f,0}+out*.37f,along,spec.residential?"RESIDENTS":names[seed%8],std::min(.15f,width*.033f),{.95f,.85f,.59f});
    for(int k=0;k<12;++k) {
        float a=-width+k*width/6,b=a+width/6;
        Vec3 color=k%2?awning:Vec3{.81f,.76f,.59f};
        addQuad(m,p+along*a+Vec3{0,3.2f,0}+out*.25f,p+along*b+Vec3{0,3.2f,0}+out*.25f,p+along*b+Vec3{0,2.85f,0}+out*1.8f,p+along*a+Vec3{0,2.85f,0}+out*1.8f,color);
        addQuad(m,p+along*a+Vec3{0,2.85f,0}+out*1.8f,p+along*b+Vec3{0,2.85f,0}+out*1.8f,p+along*b+Vec3{0,2.63f,0}+out*1.8f,p+along*a+Vec3{0,2.63f,0}+out*1.8f,color);
    }
    addBox(m,spec.door()+Vec3{0,1.38f,0},{.83f,1.38f,.06f},{.09f,.17f,.18f},yaw,2);
    addBox(m,p+along*(width*.65f+.54f)+Vec3{0,1.15f,0}+out*.4f,{.035f,.26f,.045f},{.74f,.71f,.58f},yaw,1);
    if(illuminated) {
        Vec3 light=p+Vec3{0,3.07f,0}+out*.65f,across=along*std::min(1.4f,width*.23f),depth=out*.08f;
        addQuad(m,light-across+depth,light+across+depth,light+across-depth,light-across-depth,{1,.78f,.46f},2);
        chunk.lights.push_back({light,11,{1,.78f,.46f},22,{0,-1,0},-.15f});
    }
}
void roundWindows(Mesh& m,Vec3 p,float radius,float height,int floors) {
    constexpr int sides=20;
    for(int level=0;level<floors;++level)for(int face=0;face<sides;++face) {
        float a=2*Pi*(face+.10f)/sides,b=2*Pi*(face+.90f)/sides;
        float y=level*height/floors+.7f,top=std::min(height-.4f,y+height/floors*.65f);
        Vec3 aa{std::cos(a)*radius,y,std::sin(a)*radius},bb{std::cos(b)*radius,y,std::sin(b)*radius};
        addQuad(m,p+bb,p+aa,p+Vec3{aa.x,top,aa.z},p+Vec3{bb.x,top,bb.z},{.16f,.29f,.32f},2);
    }
}
void building(Chunk& chunk,const worldGeometry::BuildingSpec& spec,WorldLod lod=WorldLod::Detail) {
    const bool detail=lod==WorldLod::Detail;
    const Vec3 p=spec.position;const float hx=spec.halfWidth,hz=spec.halfDepth,h=spec.height;
    const uint32_t seed=spec.seed;const bool suburban=spec.suburban;
    const Vec3 c=spec.color;Mesh& m=chunk.mesh;const int style=spec.style;
    auto box=[&](Vec3 center,Vec3 half,Vec3 color,float yaw=0,float material=0) {
        if(detail) {addBox(m,center,half,color,yaw,material);return;}
        auto pnt=[&](float x,float y,float z){return center+rotated({x*half.x,y*half.y,z*half.z},yaw);};
        addQuad(m,pnt(-1,-1,1),pnt(1,-1,1),pnt(1,1,1),pnt(-1,1,1),color,material);
        addQuad(m,pnt(1,-1,-1),pnt(-1,-1,-1),pnt(-1,1,-1),pnt(1,1,-1),color,material);
        addQuad(m,pnt(1,-1,1),pnt(1,-1,-1),pnt(1,1,-1),pnt(1,1,1),color,material);
        addQuad(m,pnt(-1,-1,-1),pnt(-1,-1,1),pnt(-1,1,1),pnt(-1,1,-1),color,material);
        addQuad(m,pnt(-1,1,1),pnt(1,1,1),pnt(1,1,-1),pnt(-1,1,-1),color,material);
    };
    auto cylinder=[&](Vec3 base,float radius,float height,Vec3 color,int sides,float material=0) {
        if(detail) {addCylinder(m,base,radius,height,color,sides,material);return;}
        sides=std::min(sides,lod==WorldLod::Far?8:12);
        const uint32_t first=static_cast<uint32_t>(m.vertices.size());
        for(int ring=0;ring<2;++ring)for(int side=0;side<sides;++side) {
            const float angle=2*Pi*side/sides;const Vec3 normal{std::cos(angle),0,std::sin(angle)};
            m.vertices.push_back({base+normal*radius+Vec3{0,ring?height:0,0},normal,color,material});
        }
        for(int side=0;side<sides;++side) {
            const uint32_t a=first+uint32_t(side),b=first+uint32_t((side+1)%sides),rise=uint32_t(sides);
            m.indices.insert(m.indices.end(),{b,a,a+rise,b,a+rise,b+rise});
        }
        const uint32_t center=static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({base+Vec3{0,height,0},{0,1,0},color,material});
        for(int side=0;side<sides;++side) {
            const float angle=2*Pi*side/sides;
            m.vertices.push_back({base+Vec3{std::cos(angle)*radius,height,std::sin(angle)*radius},{0,1,0},color,material});
        }
        for(int side=0;side<sides;++side)m.indices.insert(m.indices.end(),{center,center+1+uint32_t((side+1)%sides),center+1+uint32_t(side)});
    };
    auto spire=[&](Vec3 base,float radius,float height,Vec3 color,int sides) {
        cone(m,base,radius,height,color,detail?sides:std::min(sides,lod==WorldLod::Far?8:12));
    };
    if(suburban) {
        box(p+Vec3{0,h*.5f,0},{hx,h*.5f,hz},c);if(detail)windows(m,p,hx,hz,h,seed,false);
        roofGable(m,p+Vec3{0,h,0},hx+.7f,hz+.7f,2.6f,{.49f,.23f,.15f});
        box(p+Vec3{hx*.6f,h+1.5f,hz*.2f},{.8f,1.5f,.8f},c*.85f);
        if(detail) {
        box(p+Vec3{0,1.4f,-hz-.035f},{.8f,1.4f,.08f},{.20f,.29f,.28f});
        box(p+Vec3{0,2.9f,-hz-1.5f},{3,.16f,1.8f},{.84f,.77f,.61f});
        for(float dx:{-2.5f,2.5f})box(p+Vec3{dx,1.4f,-hz-2.7f},{.13f,1.4f,.13f},{.89f,.84f,.71f});
        }
    } else {
        constexpr float podium=4.7f;
        box(p+Vec3{0,podium*.5f,0},{hx,podium*.5f,hz},c*.82f);
        if(lod!=WorldLod::Far)box(p+Vec3{0,podium,0},{hx+.45f,.18f,hz+.45f},c*1.08f);
        if(style==4 && h>20) {
            float radius=std::min(hx,hz)*.96f;
            cylinder(p+Vec3{0,podium,0},radius,h-podium,c,20);
            if(detail)roundWindows(m,p+Vec3{0,podium,0},radius+.045f,h-podium,std::max(3,int((h-podium)/3.5f)));
            cylinder(p+Vec3{0,h,0},radius+1,.35f,c*.72f,20);
            cylinder(p+Vec3{0,h+.35f,0},radius*.66f,3.0f,{.28f,.40f,.39f},20,1);
            spire(p+Vec3{0,h+3.35f,0},radius*.75f,4.5f,{.34f,.49f,.46f},20);
        } else {
            float bx=hx,bz=hz,base=podium;int tiers=(style==0||style==1)?3:1;
            for(int tier=0;tier<tiers;++tier) {
                float part=(h-podium)*(tiers==1?1.0f:tier==0?.62f:.19f);
                box(p+Vec3{0,base+part*.5f,0},{bx,part*.5f,bz},c*(1.0f-tier*.04f));
                if(detail)windows(m,p+Vec3{0,base-.4f,0},bx,bz,part+.2f,seed+uint32_t(tier)*71,true);
                if(lod!=WorldLod::Far)box(p+Vec3{0,base+part-.08f,0},{bx+.3f,.17f,bz+.3f},c*.76f);
                base+=part;bx*=.78f;bz*=.78f;
            }
            if(style==0) {
                if(detail)for(float dx:{-.70f,-.35f,.35f,.70f})box(p+Vec3{dx*hx,podium+(h-podium)*.30f,-hz-.10f},{.24f,(h-podium)*.30f,.16f},{.87f,.73f,.52f});
                box(p+Vec3{0,h+2.8f,0},{hx*.29f,2.8f,hz*.29f},c*.88f);
                spire(p+Vec3{0,h+5.6f,0},3,4,{.33f,.49f,.45f},4);
            } else if(style==1) {
                box(p+Vec3{0,h+2.2f,0},{hx*.48f,2.2f,hz*.4f},{.18f,.28f,.32f},0,2);
                for(float dx:{-.8f,.8f})cylinder(p+Vec3{dx*hx*.5f,h+4.4f,0},.17f,7,{.60f,.66f,.65f},5,1);
            } else if(style==2) {
                for(int roof=0;roof<3;++roof)roofGable(m,p+Vec3{-hx+(roof+.5f)*hx*2/3,h,0},hx/3+.18f,hz+.5f,3.2f,{.29f,.32f,.30f});
                if(detail)for(int row=1;row<int(h/3.5f);++row)box(p+Vec3{0,row*3.5f,-hz-.045f},{hx,.06f,.05f},{.63f,.43f,.30f});
                cylinder(p+Vec3{hx*.72f,h-1,hz*.55f},1.25f,7,{.47f,.28f,.20f},8);
            } else if(style==3) {
                if(detail)for(float y=8;y<h-1;y+=5.5f) {
                    box(p+Vec3{0,y,-hz-.65f},{hx+.5f,.12f,.85f},c*.92f);
                    box(p+Vec3{0,y+.8f,-hz-1.4f},{hx+.4f,.10f,.045f},{.26f,.36f,.36f},0,1);
                    for(int k=-3;k<=3;++k)box(p+Vec3{k*hx/3.2f,y+.42f,-hz-1.4f},{.04f,.42f,.045f},{.26f,.36f,.36f},0,1);
                }
                box(p+Vec3{0,h+1,0},{hx+.7f,.18f,hz+.7f},c);
                if(detail)for(float dx:{-.85f,.85f})for(float dz:{-.85f,.85f})box(p+Vec3{dx*hx,h+.5f,dz*hz},{.12f,.5f,.12f},c);
            } else {
                roofGable(m,p+Vec3{0,h,0},hx+.6f,hz+.6f,6.5f,{.24f,.33f,.32f});
                if(detail)for(float dx:{-.7f,0.0f,.7f})box(p+Vec3{dx*hx,podium+(h-podium)*.5f,-hz-.20f},{.42f,(h-podium)*.5f,.3f},c*1.1f);
            }
        }
        if(detail) {
        shopfront(chunk,buildingShopfront(spec,false));
        shopfront(chunk,buildingShopfront(spec,true));
        }
        box(p+Vec3{-hx*.32f,h+1.0f,hz*.2f},{2.3f,.8f,1.45f},{.45f,.48f,.47f},0,1);
    }
    if(detail)chunk.solids.push_back({p+Vec3{-hx,0,-hz},p+Vec3{hx,h,hz}});
    if(lod!=WorldLod::Far)box(p+Vec3{0,.23f,0},{hx+.18f,.23f,hz+.18f},c*.67f);
}
void coarseTree(Mesh& m,const worldGeometry::TreeSpec& spec,WorldLod lod=WorldLod::Medium) {
    using worldGeometry::TreeKind;
    const Vec3 p=spec.position;const float s=spec.scale;
    const bool palmTree=spec.kind==TreeKind::Palm,cypressTree=spec.kind==TreeKind::Cypress,mangroveTree=spec.kind==TreeKind::Mangrove;
    float h=(palmTree?8+random01(spec.seed)*3:cypressTree?9.8f:mangroveTree?4.7f:7.7f)*s;
    Vec3 top=p+Vec3{0,h,0};
    if(palmTree) {
        const float angle=random01(spec.seed+41)*2*Pi;
        top+=Vec3{std::cos(angle)*s*.65f,0,std::sin(angle)*s*.65f};
    }
    const Vec3 trunkTop=top-Vec3{0,palmTree?0:h*.25f,0};
    const uint32_t trunk=static_cast<uint32_t>(m.vertices.size());
    for(int ring=0;ring<2;++ring)for(int side=0;side<3;++side) {
        const float angle=2*Pi*side/3;const Vec3 normal{std::cos(angle),0,std::sin(angle)};
        m.vertices.push_back({(ring?trunkTop:p)+normal*((ring?.10f:.24f)*s),normal,{.36f,.29f,.19f},0});
    }
    for(uint32_t side=0;side<3;++side) {
        const uint32_t next=(side+1)%3;
        m.indices.insert(m.indices.end(),{trunk+side,trunk+next,trunk+next+3,trunk+side,trunk+next+3,trunk+side+3});
    }
    if(palmTree) {
        const uint32_t root=static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({top,{0,1,0},{.13f,.33f,.08f},0});
        const int stride=lod==WorldLod::Far?2:1;
        for(int i=0;i<8;i+=stride) {
            const float angle=2*Pi*i/8+random01(spec.seed+17)*2;
            const Vec3 dir{std::cos(angle),0,std::sin(angle)},side{-dir.z,0,dir.x};
            const float span=(3.9f+random01(spec.seed+static_cast<uint32_t>(i)*37)*.9f)*s;
            const Vec3 bend=top+dir*(span*.48f)+Vec3{0,s,0},tip=top+dir*span-Vec3{0,1.65f*s,0};
            const uint32_t first=static_cast<uint32_t>(m.vertices.size());
            m.vertices.push_back({bend-side*(.48f*s),{0,1,0},{.13f,.33f,.08f},0});
            m.vertices.push_back({tip,{0,1,0},{.13f,.33f,.08f},0});
            m.vertices.push_back({bend+side*(.48f*s),{0,1,0},{.15f,.35f,.09f},0});
            m.indices.insert(m.indices.end(),{root,first,first+1,root,first+1,first+2});
        }
    } else {
        // An indexed octahedral crown keeps every seeded tree while bounding per-tree memory.
        const float radius=(cypressTree?2.9f:mangroveTree?4.1f:4.8f)*s;
        const float centerHeight=h*(cypressTree?.67f:.72f),halfHeight=h*(cypressTree?.43f:.35f);
        const Vec3 center=p+Vec3{0,centerHeight,0};
        const Vec3 points[]={{0,halfHeight,0},{-radius,0,0},{0,0,radius},{radius,0,0},{0,0,-radius},{0,-halfHeight,0}};
        const uint32_t base=static_cast<uint32_t>(m.vertices.size());
        for(const Vec3 v:points)m.vertices.push_back({center+v,normalized(v),cypressTree?Vec3{.15f,.29f,.16f}:Vec3{.15f,.32f,.10f},0});
        constexpr uint32_t faces[]={0,2,1,0,3,2,0,4,3,0,1,4,5,1,2,5,2,3,5,3,4,5,4,1};
        for(const uint32_t index:faces)m.indices.push_back(base+index);
    }
}
void planter(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    addBox(m,p+Vec3{0,.35f,0},{2.2f,.35f,2.2f},{.58f,.55f,.45f});
    addBox(m,p+Vec3{0,.73f,0},{1.9f,.04f,1.9f},{.20f,.28f,.10f});p.y+=.76f;palm(m,p,scale,seed);
}
void solidBox(Chunk& chunk,Vec3 center,Vec3 half,float yaw=0) {
    const float c=std::abs(std::cos(yaw)),s=std::abs(std::sin(yaw));
    const Vec3 extent{half.x*c+half.z*s,half.y,half.x*s+half.z*c};
    chunk.solids.push_back({center-extent,center+extent});
}
constexpr float BenchSeatTop=.40f;
struct BenchSpec {Vec3 origin;float yaw=0;};
BenchSpec plazaBench(float x,float z,float height,int index) {
    // The fourth original bench overlapped the market building. Its usable home is the south court.
    return {{x+26+index*25,height+.08f,z+((x==0&&z==0&&index==3)?88.0f:41.0f)},0};
}
struct ShelterSpec {
    Vec3 origin;float yaw=0;
    Vec3 along() const {return rotated({0,0,1},yaw);}
    Vec3 across() const {return rotated({1,0,0},yaw);}
    BenchSpec bench() const {return {origin-across()*.1f+Vec3{0,.02f,0},yaw+Pi*.5f};}
};
ShelterSpec furnitureShelter(Vec3 p,float yaw) {
    return {p+rotated({4,0,21},yaw),yaw};
}
Vec3 marketStall(Vec3 arcade,int index) {return arcade+Vec3{0,0,index*19.0f};}
void bench(Chunk& chunk,const BenchSpec& spec) {
    Mesh& m=chunk.mesh;const Vec3 p=spec.origin;const float yaw=spec.yaw;
    const auto part=[&](Vec3 offset,Vec3 half,Vec3 color,float material=0) {
        const Vec3 center=p+rotated(offset,yaw);
        addBox(m,center,half,color,yaw,material);
        solidBox(chunk,center,half,yaw);
    };
    part({0,BenchSeatTop-.06f,0},{1.3f,.06f,.40f},{.49f,.29f,.13f});
    part({0,.70f,.34f},{1.3f,.30f,.07f},{.49f,.29f,.13f});
    for(float a:{-.92f,.92f})part({a,.16f,0},{.08f,.16f,.32f},{.18f,.22f,.23f},1);
}
void streetFurniture(Chunk& chunk,Vec3 p,float yaw,uint32_t seed) {
    Mesh& m=chunk.mesh;Vec3 along=rotated({0,0,1},yaw),across=rotated({1,0,0},yaw);
    Vec3 bin=p+along*14;
    addCylinder(m,bin,.34f,.88f,{.18f,.27f,.24f},10,1);
    addCylinder(m,bin+Vec3{0,.9f,0},.38f,.10f,{.31f,.39f,.33f},10,1);
    for(int slat=0;slat<8;++slat) {
        float a=slat*Pi*.25f;addBox(m,bin+Vec3{std::cos(a)*.34f,.42f,std::sin(a)*.34f},{.025f,.35f,.025f},{.08f,.13f,.12f});
    }
    chunk.solids.push_back({bin-Vec3{.33f,0,.33f},bin+Vec3{.33f,1,.33f}});
    Vec3 hydrant=p-along*22;
    addCylinder(m,hydrant,.17f,.68f,{.70f,.30f,.13f},8);
    addCylinder(m,hydrant+Vec3{0,.62f,0},.24f,.12f,{.83f,.59f,.24f},8);
    addBox(m,hydrant+Vec3{0,.42f,0},{.34f,.08f,.08f},{.75f,.40f,.19f},yaw,1);
    if(seed%3==0) {
        const auto shelterSpec=furnitureShelter(p,yaw);const Vec3 shelter=shelterSpec.origin;
        for(float a:{-2.8f,2.8f})for(float b:{-.85f,.85f}) {
            const Vec3 post=shelter+along*a+across*b+Vec3{0,1.5f,0};
            addBox(m,post,{.065f,1.5f,.065f},{.23f,.32f,.33f},yaw,1);
            solidBox(chunk,post,{.065f,1.5f,.065f},yaw);
        }
        addBox(m,shelter+Vec3{0,3.0f,0},{1.2f,.14f,3.25f},{.18f,.29f,.30f},yaw);
        addBox(m,shelter+across*.90f+Vec3{0,1.65f,0},{.04f,1.12f,2.75f},{.20f,.36f,.39f},yaw,2);
        solidBox(chunk,shelter+Vec3{0,3.0f,0},{1.2f,.14f,3.25f},yaw);
        solidBox(chunk,shelter+across*.90f+Vec3{0,1.65f,0},{.04f,1.12f,2.75f},yaw);
        bench(chunk,shelterSpec.bench());
        Vec3 stop=p+along*16;
        addCylinder(m,stop,.05f,3.45f,{.49f,.56f,.54f},5,1);
        addBox(m,stop+Vec3{0,3.10f,0},{.34f,.34f,.05f},{.08f,.36f,.39f},yaw,1);
        addBox(m,stop+Vec3{0,3.10f,0}+rotated({0,0,-.055f},yaw),{.22f,.16f,.012f},{.91f,.83f,.59f},yaw);
    }
    if(seed%4==0) {
        Vec3 box=p-along*10;
        addBox(m,box+Vec3{0,.58f,0},{.36f,.58f,.28f},{.63f,.24f,.11f},yaw);
        addBox(m,box+Vec3{0,.88f,0}+rotated({0,0,-.29f},yaw),{.28f,.20f,.016f},{.81f,.77f,.61f},yaw);
    }
}
void clockPavilion(Chunk& chunk,Vec3 p,WorldLod lod=WorldLod::Detail) {
    Mesh& m=chunk.mesh;
    addBox(m,p+Vec3{0,1.5f,0},{4.6f,1.5f,4.6f},{.56f,.51f,.38f});
    addBox(m,p+Vec3{0,10.6f,0},{3.6f,7.6f,3.6f},{.76f,.69f,.51f});
    for(float y:{3.2f,15.8f,18.2f})addBox(m,p+Vec3{0,y,0},{4.0f,.22f,4.0f},{.88f,.80f,.58f});
    addCylinder(m,p+Vec3{0,18.4f,0},4.7f,1,{.30f,.43f,.39f},8,1);
    cone(m,p+Vec3{0,19.4f,0},5.4f,5.8f,{.28f,.45f,.39f},8);
    addCylinder(m,p+Vec3{0,25.2f,0},.14f,2.5f,{.68f,.66f,.48f},6,1);
    if(lod==WorldLod::Detail)for(int face=0;face<4;++face) {
        float yaw=face*Pi*.5f;Vec3 out=rotated({0,0,-1},yaw),along=rotated({1,0,0},yaw);
        Vec3 center=p+out*3.63f+Vec3{0,13.0f,0};
        addBox(m,center,{2.55f,2.55f,.035f},{.17f,.26f,.24f},yaw);
        for(int s=0;s<24;++s) {
            float a=2*Pi*s/24,b=2*Pi*(s+1)/24;
            facadeLine(m,center+out*.055f,along,2.18f*std::cos(a),2.18f*std::sin(a),2.18f*std::cos(b),2.18f*std::sin(b),.10f,{.85f,.77f,.49f},1);
        }
        for(int hour=0;hour<12;++hour) {
            float a=2*Pi*hour/12;
            facadeLine(m,center+out*.065f,along,1.76f*std::cos(a),1.76f*std::sin(a),2.03f*std::cos(a),2.03f*std::sin(a),.12f,{.91f,.85f,.66f},2);
        }
        facadeLine(m,center+out*.08f,along,0,0,.70f,1.15f,.14f,{.97f,.93f,.75f},2);
        facadeLine(m,center+out*.09f,along,0,0,-1.42f,.68f,.10f,{.97f,.93f,.75f},2);
        signText(m,p+out*3.72f+Vec3{0,5.6f,0},along,"TIDE HALL",.10f,{.95f,.83f,.56f});
        for(float col:{-2.9f,2.9f})addBox(m,p+out*3.70f+along*col+Vec3{0,8.7f,0},{.17f,5.25f,.12f},{.84f,.76f,.56f},yaw);
    }
    if(lod==WorldLod::Detail)chunk.solids.push_back({p-Vec3{4.6f,0,4.6f},p+Vec3{4.6f,26,4.6f}});
}
void marketArcade(Chunk& chunk,Vec3 p,WorldLod lod=WorldLod::Detail) {
    Mesh& m=chunk.mesh;
    for(int shop=0;shop<3;++shop) {
        Vec3 q=marketStall(p,shop);
        addBox(m,q+Vec3{0,2.5f,0},{7,2.5f,7.7f},{.73f,.63f,.43f});
        if(lod==WorldLod::Detail)shopfront(chunk,{q+Vec3{-7.05f,0,0},6.8f,Pi*.5f,static_cast<uint32_t>(shop*3+2),true});
        roofGable(m,q+Vec3{0,5,0},7.6f,8.3f,2.6f,{.48f,.24f,.13f});
        if(lod==WorldLod::Detail) {
        addBox(m,q+Vec3{-8.1f,1.0f,8.2f},{1.7f,1,1.4f},{.22f,.34f,.20f});
        for(int crate=0;crate<3;++crate) {
            Vec3 r=q+Vec3{-9.2f,1.45f,float(crate)*.85f-1};
            addBox(m,r,{.62f,.25f,.35f},{.53f,.35f,.17f});
            for(int fruit=0;fruit<4;++fruit)addCylinder(m,r+Vec3{-.40f+fruit*.27f,.26f,0},.14f,.21f,shop==0?Vec3{.85f,.52f,.10f}:Vec3{.61f,.19f,.08f},6);
        }
        }
        if(lod==WorldLod::Detail)chunk.solids.push_back({q-Vec3{7,0,7.7f},q+Vec3{7,6,7.7f}});
    }
}
void landmarkPlaza(Chunk& c,const World& w,float x,float z,const worldGeometry::BlockSpec& block,WorldLod lod=WorldLod::Detail) {
    Mesh& m=c.mesh;float h=w.height(x+64,z+64);
    groundPatch(m,w,x+15,z+15,x+113,z+113,{.63f,.63f,.56f},.05f);
    if(block.exchange) {
        Vec3 p{x+64,h,z+64};
        for(const auto& spec:block.buildings)building(c,spec,lod);
        addBox(m,p+Vec3{0,101,0},{8,6,9},{.73f,.77f,.72f},0,1);
        addCylinder(m,p+Vec3{0,107,0},.32f,12,{.81f,.81f,.71f},8,1);
    } else {
        addCylinder(m,{x+64,h+.1f,z+64},12,.7f,{.62f,.63f,.56f},24);
        addCylinder(m,{x+64,h+.83f,z+64},10,.03f,{.14f,.49f,.58f},24,3);
        addCylinder(m,{x+64,h+.85f,z+64},2.0f,1.2f,{.67f,.63f,.51f},12);
        addBox(m,{x+64,h+4.5f,z+64},{.7f,3.0f,.7f},{.54f,.68f,.63f},.7f,1);
        addBox(m,{x+64,h+6.9f,z+64},{3.5f,.55f,.55f},{.54f,.68f,.63f},-.45f,1);
        if(lod==WorldLod::Detail) {
            c.solids.push_back({{x+62,h,z+62},{x+66,h+8,z+66}});
            // Thin circumscribed bands cover the entire basin, including its shallow water.
            for(int band=0;band<24;++band) {
                const float low=-12.0f+band,high=low+1;
                const float nearest=low>0?low:high<0?-high:0;
                const float radius=std::sqrt(144-nearest*nearest);
                c.solids.push_back({{x+64-radius,h,z+64+low},{x+64+radius,h+.86f,z+64+high}});
            }
        }
    }
    if(c.x==0 && c.z==0) {
        clockPavilion(c,{x+44,h,z+98},lod);
        marketArcade(c,{x+96,h,z+35},lod);
        if(lod==WorldLod::Detail)for(float px:{20.0f,31.0f,42.0f,53.0f}) {
            addCylinder(m,{x+px,h,z+18},.15f,.90f,{.21f,.28f,.26f},8,1);
            addCylinder(m,{x+px,h+.64f,z+18},.17f,.09f,{.79f,.69f,.42f},8,1);
        }
    }
    for(int k=0;k<4;++k) for(int j=0;j<2;++j) {
        float px=x+24+k*26,pz=z+(j?106:22);
        if(lod==WorldLod::Detail)planter(m,{px,w.height(px,pz),pz},1.05f,seedAt(k,j,42));
        else coarseTree(m,{{px,w.height(px,pz)+.76f,pz},1.05f,seedAt(k,j,42),worldGeometry::TreeKind::Palm},lod);
    }
    if(lod==WorldLod::Detail)for(int i=0;i<4;++i)bench(c,plazaBench(x,z,h,i));
}
void clinicLaunch(Chunk& c,WorldLod lod=WorldLod::Detail) {
    Mesh& m=c.mesh;const Vec3 origin=ClinicLaunch.origin;
    const Vec3 hullColor{.76f,.77f,.67f},trim{.18f,.25f,.24f},rescueRed{.70f,.15f,.10f};
    const auto& stations=ClinicLaunch.stations;const auto& widths=ClinicLaunch.widths;
    Vec3 hull[6][6];
    for(int row=0;row<6;++row) {
        float bow=row/5.0f,top=.93f+.23f*bow*bow;
        hull[row][0]=origin+Vec3{-widths[row],top,stations[row]};
        hull[row][1]=origin+Vec3{-widths[row]*.80f,-.08f,stations[row]};
        hull[row][2]=origin+Vec3{-widths[row]*.12f,-.56f+.28f*bow*bow,stations[row]};
        hull[row][3]=origin+Vec3{widths[row]*.12f,-.56f+.28f*bow*bow,stations[row]};
        hull[row][4]=origin+Vec3{widths[row]*.80f,-.08f,stations[row]};
        hull[row][5]=origin+Vec3{widths[row],top,stations[row]};
    }
    if(lod!=WorldLod::Detail) {
        for(int row=0;row<5;++row) {
            for(int face=0;face<5;++face)addQuad(m,hull[row][face],hull[row][face+1],hull[row+1][face+1],hull[row+1][face],face==2?trim:hullColor);
            const float deckHeight=row<3?.58f:1.04f;
            addQuad(m,origin+Vec3{-widths[row]*.88f,deckHeight,stations[row]},origin+Vec3{-widths[row+1]*.88f,deckHeight,stations[row+1]},origin+Vec3{widths[row+1]*.88f,deckHeight,stations[row+1]},origin+Vec3{widths[row]*.88f,deckHeight,stations[row]},hullColor*.79f);
        }
        for(int end:{0,5})for(int k=1;k<5;++k) {
            if(end==0)tri(m,hull[end][0],hull[end][k+1],hull[end][k],hullColor);
            else tri(m,hull[end][0],hull[end][k],hull[end][k+1],hullColor);
        }
        addBox(m,origin+Vec3{0,2.58f,-.80f},{1.32f,.09f,1.93f},{.81f,.80f,.67f});
        addBox(m,origin+Vec3{0,2.60f,-2.74f},{1.33f,.12f,.06f},rescueRed);
        addBox(m,origin+Vec3{0,1.06f,.64f},{.52f,.45f,.42f},hullColor);
        addBox(m,origin+Vec3{.58f,.46f,-4.53f},{.32f,.37f,.32f},trim,.24f,1);
        for(float side:{-1.0f,1.0f})for(float z:{-2.50f,.85f})branch(m,origin+Vec3{side*1.18f,.66f,z},origin+Vec3{side*1.18f,2.56f,z},.038f,.032f,trim,3);
        addCylinder(m,origin+Vec3{.87f,3.22f,-2.13f},.14f,.23f,{1,.12f,.055f},4,2);
        return;
    }
    for(int row=0;row<5;++row) {
        for(int face=0;face<5;++face)addQuad(m,hull[row][face],hull[row][face+1],hull[row+1][face+1],hull[row+1][face],face==2?trim:hullColor);
        for(float side:{-1.0f,1.0f}) {
            Vec3 a=origin+Vec3{side*widths[row],hull[row][0].y-origin.y,stations[row]};
            Vec3 b=origin+Vec3{side*widths[row+1],hull[row+1][0].y-origin.y,stations[row+1]};
            branch(m,a,b,.045f,.045f,trim,5);
            Vec3 lowA=a+Vec3{0,-.29f,0},lowB=b+Vec3{0,-.29f,0};
            if(side<0)addQuad(m,a,lowA,lowB,b,rescueRed);
            else addQuad(m,b,lowB,lowA,a,rescueRed);
            Vec3 innerA=origin+Vec3{side*widths[row]*.88f,.62f,stations[row]},innerB=origin+Vec3{side*widths[row+1]*.88f,.62f,stations[row+1]};
            if(side<0)addQuad(m,a,b,innerB,innerA,hullColor*.88f);
            else addQuad(m,innerA,innerB,b,a,hullColor*.88f);
        }
        float deckHeight=row<3?.58f:1.04f;
        addQuad(m,origin+Vec3{-widths[row]*.88f,deckHeight,stations[row]},origin+Vec3{-widths[row+1]*.88f,deckHeight,stations[row+1]},origin+Vec3{widths[row+1]*.88f,deckHeight,stations[row+1]},origin+Vec3{widths[row]*.88f,deckHeight,stations[row]},hullColor*.79f);
    }
    for(int end:{0,5})for(int k=1;k<5;++k) {
        if(end==0)tri(m,hull[end][0],hull[end][k+1],hull[end][k],hullColor);
        else tri(m,hull[end][0],hull[end][k],hull[end][k+1],hullColor);
    }
    // Empty treatment benches and a narrow helm leave space for mission-specific occupants.
    for(float side:{-1.0f,1.0f}) {
        addBox(m,origin+Vec3{side*1.03f,.76f,-1.55f},{.36f,.18f,1.25f},{.31f,.43f,.39f});
        addBox(m,origin+Vec3{side*1.35f,1.05f,-1.55f},{.09f,.22f,1.25f},{.37f,.48f,.42f});
        for(float z:{-2.50f,.85f})branch(m,origin+Vec3{side*1.18f,.66f,z},origin+Vec3{side*1.18f,2.56f,z},.038f,.032f,trim,5);
    }
    addBox(m,origin+Vec3{0,2.58f,-.80f},{1.32f,.09f,1.93f},{.81f,.80f,.67f});
    addBox(m,origin+Vec3{0,2.60f,-2.74f},{1.33f,.12f,.06f},rescueRed);
    addBox(m,origin+Vec3{0,1.06f,.64f},{.52f,.45f,.42f},hullColor);
    addQuad(m,origin+Vec3{-.53f,1.44f,.99f},origin+Vec3{.53f,1.44f,.99f},origin+Vec3{.48f,1.98f,.74f},origin+Vec3{-.48f,1.98f,.74f},{.13f,.29f,.31f},1);
    for(float side:{-1.0f,1.0f})branch(m,origin+Vec3{side*.53f,1.44f,.99f},origin+Vec3{side*.48f,1.98f,.74f},.027f,.027f,trim,5);
    addBox(m,origin+Vec3{0,1.44f,.205f},{.33f,.05f,.055f},trim);
    for(int spoke=0;spoke<8;++spoke) {
        float a=2*Pi*spoke/8,b=2*Pi*(spoke+1)/8;
        branch(m,origin+Vec3{std::cos(a)*.20f,1.26f+std::sin(a)*.20f,.17f},origin+Vec3{std::cos(b)*.20f,1.26f+std::sin(b)*.20f,.17f},.022f,.022f,trim,4);
    }
    // A tilted, uncovered outboard and dark propeller identify the disabled craft.
    addBox(m,origin+Vec3{.58f,.46f,-4.53f},{.32f,.37f,.32f},trim,.24f,1);
    addBox(m,origin+Vec3{.58f,.89f,-4.55f},{.39f,.11f,.37f},{.44f,.49f,.44f},-.32f,1);
    branch(m,origin+Vec3{.58f,.24f,-4.53f},origin+Vec3{.58f,-.39f,-4.92f},.09f,.06f,trim,6);
    addBox(m,origin+Vec3{.58f,-.39f,-4.95f},{.31f,.055f,.045f},trim,.38f,1);
    for(int scar=0;scar<5;++scar)addBox(m,origin+Vec3{-1.62f,.31f+scar*.035f,-1.9f+scar*.27f},{.015f,.021f,.29f},{.36f,.37f,.29f});
    for(float side:{-1.0f,1.0f}) {
        Vec3 badge=origin+Vec3{side*1.337f,2.57f,-.8f};
        addBox(m,badge,{.035f,.13f,1.62f},rescueRed);
        signText(m,badge+Vec3{side*.041f,-.094f,0},{0,0,side},"COAST CARE",.032f,{.96f,.92f,.77f});
    }
    Vec3 mast=origin+Vec3{1.05f,2.66f,-2.25f};
    branch(m,mast,mast+Vec3{-.18f,.56f,.12f},.045f,.038f,trim,5);
    Vec3 lens=mast+Vec3{-.18f,.56f,.12f};
    addCylinder(m,lens,.14f,.23f,{1,.12f,.055f},8,2);
    addCylinder(m,lens+Vec3{0,.23f,0},.16f,.035f,trim,8,1);
    c.lights.push_back({lens,18,{1,.12f,.055f},32,{0,-1,0},-1});
    c.solids.push_back({origin+Vec3{-1.29f,-.65f,-5.02f},origin+Vec3{1.29f,1.20f,-3.0f}});
    c.solids.push_back({origin+Vec3{-1.64f,-.65f,-3.0f},origin+Vec3{1.64f,2.71f,1.4f}});
    c.solids.push_back({origin+Vec3{-1.15f,-.65f,1.4f},origin+Vec3{1.15f,1.18f,3.5f}});
    c.solids.push_back({origin+Vec3{-.47f,-.40f,3.5f},origin+Vec3{.47f,1.18f,4.5f}});
}

void transportSites(Chunk& c,const World& world,WorldLod lod=WorldLod::Detail,size_t terrainIndices=0) {
    const bool detail=lod==WorldLod::Detail;
    float x0=c.x*World::ChunkSize,z0=c.z*World::ChunkSize,x1=x0+World::ChunkSize,z1=z0+World::ChunkSize;
    auto owns=[&](float x,float z){return x>=x0&&x<x1&&z>=z0&&z<z1;};
    auto patch=[&](float ax,float az,float bx,float bz,Vec3 color,float lift=0,float material=0) {
        ax=std::max(ax,x0);az=std::max(az,z0);bx=std::min(bx,x1);bz=std::min(bz,z1);
        if(bx>ax&&bz>az)groundPatch(c.mesh,world,ax,az,bx,bz,color,lift,material,false,material==4?terrainIndices:0);
    };
    Mesh& m=c.mesh;
    if(x1>-3320&&x0<-3050&&z1>-1430&&z0<-550) {
        for(size_t index=0;index<std::size(AirfieldPaving);++index) {
            const auto& area=AirfieldPaving[index];
            patch(area.x0,area.z0,area.x1,area.z1,index?Vec3{.20f,.22f,.21f}:Vec3{.13f,.16f,.17f},.025f,4);
        }
        if(detail)for(float x:{-3216.0f,-3184.0f})patch(x-.10f,-1248,x+.10f,-752,{.82f,.82f,.68f},.038f);
        if(detail)for(int stripe=0;stripe<15;++stripe) {
            float z=-1232+stripe*32.0f;patch(-3200.17f,z,-3199.83f,z+12,{.84f,.83f,.72f},.04f);
        }
        if(detail)for(int bar=0;bar<8;++bar)for(float z:{-1248.0f,-764.0f}) {
            float x=-3213.5f+bar*3.7f;patch(x,z,x+1.3f,z+12,{.85f,.83f,.72f},.04f);
        }
        if(detail)for(float x:{-3213.0f,-3193.0f})for(float z:{-1200.0f,-812.0f})patch(x,z,x+6,z+12,{.84f,.83f,.72f},.04f);
        if(detail)for(float x:{-3220.0f,-3180.0f})for(int lamp=0;lamp<9;++lamp) {
            float z=-1256+lamp*64.0f;if(!owns(x,z))continue;
            addCylinder(m,{x,4.02f,z},.19f,.18f,{.48f,.47f,.32f},6);
            addCylinder(m,{x,4.20f,z},.12f,.11f,{.95f,.77f,.33f},6,2);
        }
        if(owns(AirfieldHangar.origin.x,AirfieldHangar.origin.z)) {
            const Vec3 p=AirfieldHangar.origin;
            addBox(m,p+Vec3{0,AirfieldHangar.half.y,0},AirfieldHangar.half,{.48f,.53f,.47f});
            roofGable(m,p+Vec3{0,AirfieldHangar.half.y*2,0},AirfieldHangar.half.x+.7f,AirfieldHangar.half.z+.7f,AirfieldHangar.roofRise,{.33f,.40f,.37f});
            addBox(m,p+Vec3{20.06f,4,0},{.07f,4,19},{.22f,.29f,.28f});
            if(detail)for(int rail=1;rail<8;++rail)addBox(m,p+Vec3{20.16f,rail*.95f,0},{.04f,.04f,19},{.53f,.58f,.51f});
            if(detail)signText(m,p+Vec3{20.2f,8.45f,0},{0,0,1},"BREAKER AIR",.19f,{.93f,.86f,.63f});
            if(detail)c.solids.push_back({p-Vec3{20,0,27},p+Vec3{20,11,27}});
        }
        if(detail&&owns(-3231,-1236)) {
            Vec3 p{-3231,4,-1236};addCylinder(m,p,.11f,6.4f,{.55f,.57f,.48f},6,1);
            for(int band=0;band<5;++band) {
                float a=band/5.0f,b=(band+1)/5.0f;
                branch(m,p+Vec3{a*2.8f,6.2f-a*.4f,0},p+Vec3{b*2.8f,6.2f-b*.4f,0},.36f-a*.22f,.36f-b*.22f,band%2?Vec3{.88f,.80f,.60f}:Vec3{.83f,.29f,.10f},8);
            }
            c.solids.push_back({p-Vec3{.12f,0,.12f},p+Vec3{.12f,6.4f,.12f}});
        }
    }
    if(x1>2494&&x0<2690&&z1>740&&z0<795) {
        patch(DockApproach.x0,DockApproach.z0,DockApproach.x1,DockApproach.z1,{.58f,.54f,.39f},.035f);
        if(!detail) {
            patch(2600,765,2667,771,{.48f,.35f,.20f});
            patch(2667,754,2675,782,{.48f,.35f,.20f});
        }
        if(detail)for(int board=0;board<34;++board) {
            float x=2600+board*2.0f;patch(x,765,std::min(x+1.97f,2667.0f),771,board%3?Vec3{.48f,.35f,.20f}:Vec3{.55f,.41f,.24f});
        }
        if(detail)for(int board=0;board<14;++board) {
            float z=754+board*2.0f;patch(2667,z,2675,z+1.97f,board%3?Vec3{.48f,.35f,.20f}:Vec3{.55f,.41f,.24f});
        }
        for(float z:{765.3f,770.7f})for(int piling=0;piling<7;++piling) {
            float x=2624+piling*8.0f;if(!owns(x,z))continue;
            float bottom=naturalHeight(x,z)-.8f;
            addCylinder(m,{x,bottom,z},.24f,.32f-bottom,{.31f,.25f,.17f},detail?7:4);
            addBox(m,{x,.18f,768},{.18f,.17f,3.1f},{.38f,.28f,.16f});
            if(detail)c.solids.push_back({{x-.24f,bottom,z-.24f},{x+.24f,.32f,z+.24f}});
        }
        auto rail=[&](Vec3 a,Vec3 b) {
            Vec3 mid=(a+b)*.5f;if(!owns(mid.x,mid.z))return;
            Vec3 half{std::max(.065f,std::abs(b.x-a.x)*.5f),.07f,std::max(.065f,std::abs(b.z-a.z)*.5f)};
            addBox(m,mid+Vec3{0,.85f,0},half,{.54f,.41f,.25f});
            for(Vec3 p:{a,b})addBox(m,p+Vec3{0,.47f,0},{.085f,.47f,.085f},{.43f,.31f,.19f});
            if(detail)c.solids.push_back({mid-Vec3{half.x,0,half.z},mid+Vec3{half.x,.92f,half.z}});
        };
        for(float z:{765.0f,771.0f})for(int section=0;section<5;++section)rail({2624+section*8.6f,.4f,z},{2624+(section+1)*8.6f,.4f,z});
        rail({2667,.4f,754},{2675,.4f,754});rail({2667,.4f,782},{2675,.4f,782});
        rail({2667,.4f,754},{2667,.4f,765});rail({2667,.4f,771},{2667,.4f,782});
        rail({2675,.4f,754},{2675,.4f,764});rail({2675,.4f,772},{2675,.4f,782});
        for(float z:{755.0f,781.0f})if(owns(2668,z)) {
            Vec3 p{2668,.4f,z};addCylinder(m,p,.10f,4,{.25f,.31f,.29f},6,1);
            addBox(m,p+Vec3{0,4,0},{.38f,.10f,.38f},{1,.76f,.43f},0,2);
            if(detail)c.lights.push_back({p+Vec3{0,3.9f,0},20,{1,.76f,.43f},55,{0,-1,0},-.15f});
            if(detail)c.solids.push_back({p-Vec3{.1f,0,.1f},p+Vec3{.1f,4,.1f}});
        }
    }
}

}

namespace worldGeometry {
BuildingSpec describeBuilding(Vec3 p,float hx,float hz,float h,uint32_t seed,bool suburban) {
    static constexpr Vec3 palette[]={{.77f,.68f,.53f},{.83f,.79f,.67f},{.60f,.66f,.66f},{.69f,.45f,.34f},{.77f,.72f,.62f},{.48f,.58f,.63f},{.88f,.77f,.61f},{.57f,.62f,.53f}};
    static constexpr Vec3 urban[]={{.77f,.53f,.38f},{.42f,.53f,.57f},{.49f,.28f,.20f},{.87f,.80f,.66f},{.64f,.70f,.66f},{.75f,.72f,.61f}};
    const int style=int(seed%6);
    if(!suburban&&style==2)h=std::min(h,22.5f);
    if(!suburban&&style==5)h=std::min(h,28.0f);
    return {p,hx,hz,h,seed,suburban,style,suburban?palette[seed%8]:urban[style]};
}
BlockSpec describeBlock(const World& world,int cx,int cz) {
    BlockSpec block;block.seed=seedAt(cx,cz);
    const float x=cx*World::ChunkSize,z=cz*World::ChunkSize;
    block.biome=world.biome(x+64,z+64);
    block.urban=block.biome==Biome::Downtown||block.biome==Biome::Residential;
    block.plaza=block.urban&&plazaBlock(cx,cz);block.exchange=block.plaza&&cx==3&&cz==3;
    if(block.plaza) {
        if(block.exchange) {
            const Vec3 p{x+64,world.height(x+64,z+64),z+64};
            block.buildings.push_back(describeBuilding(p,23,25,72,0x71346u,false));
            block.buildings.push_back(describeBuilding(p+Vec3{0,72,0},15,17,23,0x71516u,false));
        }
    } else if(block.urban) {
        for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
            const uint32_t seed=seedAt(cx*2+i,cz*2+j,71);
            if(cx==1&&cz==0&&i==0&&j==1) {block.garage=true;continue;}
            const float px=x+36+i*56,pz=z+36+j*56;
            if(world.road(px,pz)||world.biome(px,pz)==Biome::Ocean)continue;
            const bool sub=block.biome==Biome::Residential;
            const float hx=(sub?10.0f:14.0f)+random01(seed)*5,hz=(sub?11.0f:14.0f)+random01(seed+1)*6;
            float h=sub?5.9f+float(seed%3)*3.2f:12+random01(seed+2)*37;
            if(!sub&&seed%7==0)h+=18;
            block.buildings.push_back(describeBuilding({px,world.height(px,pz),pz},hx,hz,h,seed,sub));
        }
    } else if(block.biome==Biome::Countryside&&block.seed%13==0) {
        const float px=x+64,pz=z+64;
        if(!world.road(px,pz)&&!siteReserve(px,pz))block.buildings.push_back(describeBuilding({px,world.height(px,pz),pz},12,19,6.5f,block.seed,true));
    }
    return block;
}
std::vector<PedestrianPlace> describePedestrianPlaces(const World& world,int cx,int cz) {
    std::vector<PedestrianPlace> places;
    // This authored neighborhood has complete markings and known exterior destinations.
    if(std::abs(cx)>1||std::abs(cz)>1)return places;
    const auto block=describeBlock(world,cx,cz);if(!block.urban)return places;
    const float x=cx*128.0f,z=cz*128.0f;
    const uint32_t prefix=(uint32_t(cx+64)<<16)|(uint32_t(cz+64)<<8);
    auto ground=[&](Vec3 p){p.y=world.height(p.x,p.z);return p;};
    auto add=[&](int slot,int site,PedestrianPlaceKind kind,Vec3 position,Vec3 approach,float yaw,bool sheltered,Vec3 seat=Vec3{}) {
        PedestrianPlace place;place.id=prefix|uint32_t(128+slot);place.siteId=prefix|uint32_t(128+site);
        place.kind=kind;place.position=ground(position);place.approach=ground(approach);
        place.seatPosition=kind==PedestrianPlaceKind::Seat?seat:place.position;
        place.yaw=yaw;place.sheltered=sheltered;places.push_back(place);
    };
    for(const auto& building:block.buildings) {
        if(building.suburban)continue;
        const bool front=building.position.z-z<64;
        const int lot=(front?0:2)+(building.position.x-x<64?0:1);
        const auto facade=buildingShopfront(building,front);
        const Vec3 position=facade.door()+facade.outward()*1.0f;
        // Exterior entrances join the clear inter-building alleys, not a line through the facade.
        Vec3 approach=position;
        if(front)approach.x=x+(lot==0?13.2f:64.0f);
        else approach.z=z+64;
        add(lot,lot,front?PedestrianPlaceKind::Home:PedestrianPlaceKind::Work,
            position,approach,facade.yaw,true);
    }
    if(block.garage) {
        const auto garage=World::garageSite();const Vec3 position=garage.pedestrianDoor+Vec3{0,0,1.5f};
        add(8,8,PedestrianPlaceKind::Work,position,position+Vec3{0,0,2},Pi,false);
    }
    for(int edge:{0,2}) {
        const uint32_t seed=block.seed+uint32_t(edge)*13;if(seed%3!=0)continue;
        const Vec3 p=ground(edge==0?Vec3{x+12,0,z+64}:Vec3{x+64,0,z+12})+Vec3{0,.14f,0};
        const auto shelter=furnitureShelter(p,edge==0?0:-Pi*.5f);
        const Vec3 position=shelter.origin-shelter.across()*.8f+shelter.along()*2;
        add(40+edge/2,40+edge/2,PedestrianPlaceKind::Shelter,position,position-shelter.across()*2,shelter.yaw-Pi*.5f,true);
    }
    if(cx==0&&cz==0) {
        const float h=world.height(64,64);
        for(int stall=0;stall<3;++stall) {
            const Vec3 counter=marketStall({96,h,35},stall),position=counter+Vec3{-10.5f,0,0};
            add(16+stall,16+stall,PedestrianPlaceKind::Market,position,position+Vec3{-2,0,0},Pi*.5f,false);
        }
        for(int index=0;index<4;++index) {
            const auto seat=plazaBench(0,0,h,index);
            for(int side=0;side<2;++side) {
                const float offset=side?.65f:-.65f;
                const Vec3 position=seat.origin+rotated({offset,0,-1.05f},seat.yaw);
                const Vec3 pelvis=seat.origin+rotated({offset,BenchSeatTop+.11f,0},seat.yaw);
                add(24+index*2+side,24+index*2,PedestrianPlaceKind::Seat,position,
                    position+rotated({0,0,-2},seat.yaw),seat.yaw+Pi,false,pelvis);
            }
        }
        add(48,48,PedestrianPlaceKind::Conversation,{49.5f,h,63},{47.5f,h,63},0,false);
        add(49,48,PedestrianPlaceKind::Conversation,{49.5f,h,65},{47.5f,h,65},Pi,false);
        add(50,50,PedestrianPlaceKind::Conversation,{54,h,98},{54,h,96},Pi*.5f,false);
        add(51,50,PedestrianPlaceKind::Conversation,{56,h,98},{56,h,96},-Pi*.5f,false);
    }
    return places;
}
std::vector<TreeSpec> describeNaturalTrees(const World& world,int cx,int cz) {
    const float x=cx*World::ChunkSize,z=cz*World::ChunkSize;
    const Biome center=world.biome(x+64,z+64);const uint32_t seed=seedAt(cx,cz);
    std::vector<TreeSpec> trees;
    if(center==Biome::Downtown||center==Biome::Residential)return trees;
    const int count=center==Biome::Wetland?18:center==Biome::Countryside?18:center==Biome::Ocean?5:14;
    trees.reserve(static_cast<size_t>(count));
    for(int n=0;n<count;++n) {
        const uint32_t ts=seed+n*137u;const float px=x+8+random01(ts)*112,pz=z+8+random01(ts+47)*112;
        const float h=world.height(px,pz);const Biome local=world.biome(px,pz);
        if(world.road(px,pz)||siteReserve(px,pz)||h<WaterLevel-(local==Biome::Wetland?.5f:-.15f)||landDistance(px,pz)<40)continue;
        TreeKind kind=TreeKind::Broadleaf;
        if(local==Biome::Beach||local==Biome::Island)kind=TreeKind::Palm;
        else if(local==Biome::Wetland)kind=h<WaterLevel+.7f?TreeKind::Mangrove:TreeKind::Cypress;
        else if(local==Biome::Ocean)continue;
        trees.push_back({{px,h,pz},.7f+random01(ts+8)*.8f,ts,kind});
    }
    return trees;
}
namespace {
void workshopTire(Mesh& mesh,Vec3 center,float radius,float width,int segments=16,int rings=8) {
    const uint32_t first=static_cast<uint32_t>(mesh.vertices.size());
    for(int side=0;side<segments;++side)for(int ring=0;ring<rings;++ring) {
        const float a=2*Pi*side/segments,b=2*Pi*ring/rings;
        const Vec3 radial{std::cos(a),0,std::sin(a)},normal=radial*std::cos(b)+Vec3{0,std::sin(b),0};
        mesh.vertices.push_back({center+radial*(radius+width*std::cos(b))+Vec3{0,width*std::sin(b),0},normal,{.045f,.052f,.049f},0});
    }
    for(int side=0;side<segments;++side)for(int ring=0;ring<rings;++ring) {
        const uint32_t a=first+uint32_t(side*rings+ring),b=first+uint32_t(((side+1)%segments)*rings+ring);
        const uint32_t c=first+uint32_t(((side+1)%segments)*rings+(ring+1)%rings),d=first+uint32_t(side*rings+(ring+1)%rings);
        mesh.indices.insert(mesh.indices.end(),{a,d,c,a,c,b});
    }
}
void workshopRoll(Mesh& mesh,Vec3 center,float halfLength,float radius,int sides) {
    for(int side=0;side<sides;++side) {
        const float a=2*Pi*side/sides,b=2*Pi*(side+1)/sides;
        const Vec3 pa{0,std::cos(a)*radius,std::sin(a)*radius},pb{0,std::cos(b)*radius,std::sin(b)*radius};
        const Vec3 left=center-Vec3{halfLength,0,0},right=center+Vec3{halfLength,0,0};
        addQuad(mesh,left+pb,right+pb,right+pa,left+pa,{.43f,.49f,.46f},1);
        tri(mesh,left,left+pb,left+pa,{.26f,.32f,.30f},1);
        tri(mesh,right,right+pa,right+pb,{.26f,.32f,.30f},1);
    }
}
}
void appendGarage(Chunk& chunk,const World& world,WorldLod lod) {
    const auto site=World::garageSite();const auto grade=garageGrade();
    const bool detail=lod==WorldLod::Detail,medium=lod==WorldLod::Medium;
    const float floor=site.floorHeight,x0=site.shell.min.x,x1=site.shell.max.x,z0=site.shell.min.z,z1=site.shell.max.z;
    Mesh& mesh=chunk.mesh;
    const Vec3 plaster{.71f,.74f,.67f},teal{.075f,.27f,.25f},orange{.84f,.34f,.12f},steel{.30f,.36f,.34f};
    auto solid=[&](Vec3 center,Vec3 half,Vec3 color,float material=0,float yaw=0) {
        addBox(mesh,center,half,color,yaw,material);
        if(detail) {
            const float s=std::abs(std::sin(yaw)),c=std::abs(std::cos(yaw));
            const Vec3 extent{half.x*c+half.z*s,half.y,half.x*s+half.z*c};
            chunk.solids.push_back({center-extent,center+extent});
        }
    };
    auto wall=[&](float ax,float az,float bx,float bz,float bottom,float top,Vec3 color) {
        solid({(ax+bx)*.5f,floor+(bottom+top)*.5f,(az+bz)*.5f},{(bx-ax)*.5f,(top-bottom)*.5f,(bz-az)*.5f},color);
    };
    auto floorQuad=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 color) {addQuad(mesh,a,b,c,d,color);};
    // Four planar apron sectors are exactly the min-of-four-ramps support profile.
    floorQuad({grade.x0,0,grade.z0},{grade.x0,0,grade.z1},{x0,floor,z1},{x0,floor,z0},{.39f,.42f,.38f});
    floorQuad({grade.x1,0,grade.z1},{grade.x1,0,grade.z0},{x1,floor,z0},{x1,floor,z1},{.39f,.42f,.38f});
    floorQuad({grade.x1,0,grade.z0},{grade.x0,0,grade.z0},{x0,floor,z0},{x1,floor,z0},{.40f,.43f,.39f});
    floorQuad({grade.x0,0,grade.z1},{grade.x1,0,grade.z1},{x1,floor,z1},{x0,floor,z1},{.43f,.45f,.41f});
    floorQuad({x0,floor,z0},{x0,floor,z1},{x1,floor,z1},{x1,floor,z0},{.35f,.39f,.36f});
    // Thin volumes supply correctly oriented interior faces; both portals are physical gaps.
    wall(x0,z0,x0+.3f,z1,0,6.2f,plaster);wall(x1-.3f,z0,x1,z1,0,6.2f,plaster);
    wall(x0+.3f,z0,x1-.3f,z0+.3f,0,6.2f,plaster);
    wall(x0+.3f,z1-.3f,159,z1,0,6.2f,plaster);
    wall(161,z1-.3f,171,z1,0,6.2f,plaster);
    wall(179,z1-.3f,x1-.3f,z1,0,6.2f,plaster);
    wall(159,z1-.3f,161,z1,2.8f,6.2f,plaster);
    wall(171,z1-.3f,179,z1,4.5f,6.2f,plaster);
    addQuad(mesh,{x0,floor+5.2f,z0},{x1,floor+5.2f,z0},{x1,floor+5.2f,z1},{x0,floor+5.2f,z1},{.53f,.58f,.53f});
    if(detail)chunk.solids.push_back({{x0,floor+5.2f,z0},{x1,floor+6.2f,z1}});
    // Three original sawtooth bays retain their exact silhouette at every distance.
    for(int tooth=0;tooth<3;++tooth) {
        const float a=x0+tooth*10,b=a+10,low=floor+6.2f,high=floor+7.6f;
        addQuad(mesh,{a,low,z0},{a,low,z1},{b,high,z1},{b,high,z0},{.22f,.34f,.31f},1);
        addQuad(mesh,{b,low,z0},{b,high,z0},{b,high,z1},{b,low,z1},{.24f,.40f,.39f},2);
        tri(mesh,{a,low,z1},{b,low,z1},{b,high,z1},plaster);
        tri(mesh,{b,high,z0},{b,low,z0},{a,low,z0},plaster);
    }
    solid({159,floor+1.35f,97.05f},{.90f,1.35f,.055f},orange,1,Pi*.5f);
    workshopRoll(mesh,{site.vehicleDoor.x,floor+4.97f,z1-.05f},4,.34f,detail?12:8);
    addQuad(mesh,{156,floor+4.96f,98.32f},{182,floor+4.96f,98.32f},{182,floor+6.14f,98.32f},{156,floor+6.14f,98.32f},teal);
    if(lod==WorldLod::Far)return;
    // The office is part of the same space, with a separate open two-metre passage.
    wall(155,80,155.22f,87,0,3.8f,{.67f,.69f,.60f});wall(162.78f,80,163,87,0,3.8f,{.67f,.69f,.60f});
    wall(155.22f,80,162.78f,80.22f,0,3.8f,{.67f,.69f,.60f});
    wall(155.22f,86.78f,159,87,0,3.8f,{.67f,.69f,.60f});wall(161,86.78f,162.78f,87,0,3.8f,{.67f,.69f,.60f});
    wall(159,86.78f,161,87,2.8f,3.8f,{.67f,.69f,.60f});
    addQuad(mesh,{155,floor+3.8f,80},{163,floor+3.8f,80},{163,floor+3.8f,87},{155,floor+3.8f,87},{.72f,.73f,.65f});
    if(detail)chunk.solids.push_back({{155,floor+3.8f,80},{163,floor+3.95f,87}});
    solid({168.5f,floor+2.05f,88},{.33f,2.05f,.43f},orange,1);
    solid({181.5f,floor+2.05f,88},{.33f,2.05f,.43f},orange,1);
    solid({175,floor+4.2f,88},{6.83f,.15f,.27f},steel,1);
    solid({175,floor+.94f,79.3f},{5.5f,.12f,.7f},{.43f,.46f,.37f},1);
    if(medium)return;
    // Deep reveals, painted plinths and a legible sign make the frontage readable on foot.
    for(const auto span:std::array<Vec2,3>{{{154.3f,159},{161,171},{179,183.7f}}}) {
        addBox(mesh,{(span.x+span.y)*.5f,floor+.78f,98.035f},{(span.y-span.x)*.5f,.78f,.035f},teal);
        addBox(mesh,{(span.x+span.y)*.5f,floor+1.63f,98.045f},{(span.y-span.x)*.5f,.045f,.045f},{.83f,.73f,.44f});
    }
    for(float x:{159.0f,161.0f})addBox(mesh,{x,floor+1.42f,98.10f},{.065f,1.42f,.12f},steel,0,1);
    for(float x:{171.0f,179.0f})addBox(mesh,{x,floor+2.28f,98.08f},{.085f,2.28f,.10f},steel,0,1);
    addBox(mesh,{175,floor+4.54f,98.08f},{4.1f,.065f,.1f},steel,0,1);
    signText(mesh,{169,floor+5.08f,98.35f},{-1,0,0},"HARBOR MOTOR WORKS",.16f,{.91f,.85f,.62f},5);
    signText(mesh,{166,floor+2.35f,98.075f},{-1,0,0},"SERVICE",.075f,{.14f,.30f,.27f});
    signText(mesh,{157,floor+2.25f,98.078f},{-1,0,0},"OFFICE",.052f,{.91f,.78f,.52f});
    // The personnel door is propped fully inward, including its handle and hinge plates.
    for(float y:{.35f,2.30f})addBox(mesh,{159.08f,floor+y,97.88f},{.035f,.09f,.06f},{.70f,.66f,.48f},0,1);
    addBox(mesh,{159.08f,floor+1.18f,96.42f},{.045f,.035f,.18f},{.80f,.72f,.51f},0,1);
    tri(mesh,{159.16f,floor,96.26f},{158.89f,floor,96.26f},{159.03f,floor+.13f,96.59f},{.56f,.39f,.18f});
    for(int rib=0;rib<28;++rib) {
        const float x=171+rib*(8.0f/27);
        branch(mesh,{x,floor+4.72f,98.17f},{x,floor+5.20f,98.16f},.014f,.014f,{.69f,.72f,.65f},4);
    }
    // Brick and corrugated cladding are authored as shallow geometry rather than decals.
    for(int row=0;row<6;++row)for(int brick=0;brick<20;++brick) {
        float a=154.35f+brick*1.47f+(row%2)*.735f,b=std::min(a+1.40f,183.65f);
        if(b<=a)continue;
        const float y=floor+.18f+row*.245f;
        addQuad(mesh,{a,y,78.32f},{b,y,78.32f},{b,y+.20f,78.32f},{a,y+.20f,78.32f},row%2?Vec3{.44f,.49f,.43f}:Vec3{.47f,.52f,.46f});
    }
    for(int strip=0;strip<28;++strip) {
        const float z=78.5f+strip*.68f;
        addBox(mesh,{183.675f,floor+3.6f,z},{.025f,1.15f,.025f},{.57f,.62f,.56f},0,1);
    }
    // Structural ties leave the five-metre workshop volume open below the roof.
    for(float z:{80.5f,86.2f,96.0f}) {
        addBox(mesh,{169,floor+5.04f,z},{14.6f,.065f,.055f},steel,0,1);
        for(int brace=0;brace<6;++brace) {
            const float a=154.6f+brace*4.8f;
            branch(mesh,{a,floor+5.1f,z},{a+2.4f,floor+4.72f,z},.032f,.032f,steel,4);
            branch(mesh,{a+2.4f,floor+4.72f,z},{a+4.8f,floor+5.1f,z},.032f,.032f,steel,4);
        }
    }
    // Lift mechanisms stay folded outside the complete drive-through clearance envelope.
    for(float x:{168.5f,181.5f}) {
        solid({x,floor+.12f,88},{.60f,.12f,.66f},{.30f,.34f,.29f},1);
        addBox(mesh,{x,floor+1.55f,87.55f},{.24f,1.28f,.035f},steel,0,1);
        addCylinder(mesh,{x,floor+.35f,88.12f},.12f,3.40f,{.58f,.64f,.58f},10,1);
        for(float z:{86.9f,89.1f})solid({x,floor+.28f,z},{.31f,.16f,.76f},{.72f,.30f,.10f},1);
        for(int slot=0;slot<13;++slot)addBox(mesh,{x,floor+.6f+slot*.24f,87.50f},{.13f,.045f,.018f},{.095f,.14f,.13f});
    }
    // Back-wall bench, drawer banks, pegboard, hand tools and a stocked rolling cabinet.
    for(float x:{170,175,180}) {
        solid({x,floor+.43f,79.3f},{2.0f,.43f,.62f},teal,1);
        for(int drawer=0;drawer<4;++drawer) {
            addBox(mesh,{x,floor+.16f+drawer*.19f,79.96f},{1.86f,.072f,.035f},{.21f,.37f,.32f},0,1);
            addBox(mesh,{x,floor+.16f+drawer*.19f,80.015f},{.70f,.025f,.035f},{.64f,.65f,.52f},0,1);
        }
    }
    addBox(mesh,{175,floor+2.35f,78.39f},{6.1f,.95f,.07f},{.38f,.42f,.33f});
    for(int row=0;row<7;++row)for(int col=0;col<37;++col) {
        const float x=169.2f+col*.32f,y=floor+1.54f+row*.26f;
        addQuad(mesh,{x-.022f,y-.022f,78.465f},{x+.022f,y-.022f,78.465f},{x+.022f,y+.022f,78.465f},{x-.022f,y+.022f,78.465f},{.11f,.17f,.14f});
    }
    for(int tool=0;tool<16;++tool) {
        const float x=169.45f+tool*.73f,y=floor+1.78f+(tool%3)*.31f;
        facadeLine(mesh,{x,y,78.50f},{1,0,0},0,0,.08f,.55f,.06f,{.61f,.68f,.63f},1);
        facadeLine(mesh,{x,y,78.50f},{1,0,0},-.10f,.57f,.10f,.57f,.05f,{.61f,.68f,.63f},1);
        addCylinder(mesh,{x,floor+1.09f,79.35f},.055f,.20f,{.58f,.61f,.54f},8,1);
    }
    solid({165.6f,floor+.66f,94.0f},{.73f,.52f,.43f},orange,1);
    addBox(mesh,{165.6f,floor+1.22f,94.0f},{.79f,.06f,.48f},{.22f,.29f,.25f},0,1);
    for(int drawer=0;drawer<5;++drawer) {
        addBox(mesh,{165.6f,floor+.30f+drawer*.17f,94.46f},{.65f,.064f,.026f},{.64f,.25f,.10f},0,1);
        addBox(mesh,{165.6f,floor+.30f+drawer*.17f,94.50f},{.49f,.019f,.025f},{.78f,.74f,.59f},0,1);
    }
    for(float x:{165.05f,166.15f})for(float z:{93.66f,94.34f})addCylinder(mesh,{x,floor+.03f,z},.095f,.16f,steel,8,1);
    // Six individually modelled tires and their cradles are outside the bay and walking route.
    for(int stack=0;stack<3;++stack) {
        const Vec3 p{182.8f,floor+.27f,82.3f+stack*4.6f};
        for(int tire=0;tire<2;++tire)workshopTire(mesh,p+Vec3{0,tire*.44f,0},.47f,.18f);
        chunk.solids.push_back({p+Vec3{-.68f,-.27f,-.68f},p+Vec3{.68f,.90f,.68f}});
    }
    // Counter placement leaves the office customer and staff markers on opposite clear sides.
    solid({159.7f,floor+.51f,82.75f},{2.65f,.51f,.40f},teal);
    addBox(mesh,{159.7f,floor+1.055f,82.75f},{2.76f,.045f,.48f},{.62f,.55f,.39f});
    addBox(mesh,{157.7f,floor+1.30f,82.77f},{.32f,.21f,.045f},{.12f,.20f,.18f},0,1);
    addBox(mesh,{157.7f,floor+1.30f,82.821f},{.27f,.16f,.009f},{.30f,.57f,.49f},0,5);
    addBox(mesh,{161.5f,floor+1.13f,82.80f},{.28f,.04f,.22f},{.75f,.71f,.52f});
    addCylinder(mesh,{160.9f,floor+1.10f,82.66f},.085f,.19f,{.82f,.76f,.58f},10);
    signText(mesh,{159.8f,floor+2.30f,80.24f},{-1,0,0},"HONEST WORK",.065f,{.13f,.30f,.26f});
    for(int page=0;page<5;++page) {
        addBox(mesh,{155.245f,floor+1.95f,81.2f+page*.8f},{.012f,.30f,.24f},{.82f,.79f,.63f});
        for(int line=0;line<4;++line)addBox(mesh,{155.261f,floor+2.10f-line*.09f,81.2f+page*.8f},{.009f,.007f,.17f},{.31f,.40f,.31f});
    }
    // Directional sources stay inside the walls; their visible lenses are the emitter planes.
    auto fixture=[&](Vec3 position,float halfX,float halfZ,float radius,float intensity,float cone,bool always) {
        addBox(mesh,position+Vec3{0,.067f,0},{halfX+.065f,.055f,halfZ+.055f},{.25f,.31f,.28f},0,1);
        const Vec3 color=always?Vec3{.93f,.98f,.87f}:Vec3{1,.77f,.45f};
        addQuad(mesh,position+Vec3{-halfX,0,-halfZ},position+Vec3{halfX,0,-halfZ},position+Vec3{halfX,0,halfZ},position+Vec3{-halfX,0,halfZ},color,always?5.0f:2.0f);
        Light light{position,radius,color,intensity,{0,-1,0},cone};
        if(always)chunk.alwaysLights.push_back(light);else chunk.lights.push_back(light);
    };
    for(float x:{170.0f,178.0f})for(float z:{82.0f,88.0f,94.0f})fixture({x,floor+4.98f,z},1.10f,.17f,8,55,.68f,true);
    for(float x:{157.5f,160.5f})fixture({x,floor+3.65f,83.5f},.55f,.18f,5,22,.85f,true);
    for(float x:{156.0f,182.0f})fixture({x,floor+4.78f,98.70f},.38f,.22f,9,32,.70f,false);
    // Paint is raised by twelve millimetres, including the sloped approach markings.
    for(float x:{170.0f,180.0f})groundPatch(mesh,world,x-.055f,82,x+.055f,95,{.82f,.64f,.24f},.012f);
    for(float z:{82.0f,95.0f})groundPatch(mesh,world,170,z-.055f,180,z+.055f,{.82f,.64f,.24f},.012f);
    for(float x:{171.0f,179.0f})groundPatch(mesh,world,x-.065f,99,x+.065f,113,{.80f,.76f,.53f},.012f);
    for(int stripe=0;stripe<7;++stripe)groundPatch(mesh,world,166.0f+stripe*.45f,98.1f,166.20f+stripe*.45f,100.0f,{.83f,.65f,.22f},.012f);
}

Vec3 terrainColor(const World& world,float x,float z) {
    const Biome b=world.biome(x,z);const float d=landDistance(x,z);
    Vec3 color{.24f,.34f,.16f};
    if(b==Biome::Downtown)color={.22f,.31f,.19f};
    if(b==Biome::Residential)color={.30f,.40f,.19f};
    if(b==Biome::Beach||d<45)color={.75f,.66f,.43f};
    if(b==Biome::Wetland)color={.24f,.29f,.13f};
    if(b==Biome::Ocean)color={.24f,.43f,.35f};
    if(b==Biome::Island)color={.27f,.39f,.17f};
    return color*(.92f+random01(seedAt(int(x/8),int(z/8)))*.13f);
}
bool appendTerrain(Mesh& mesh,const World& world,int cx,int cz,WorldLod lod) {
    if(lod!=WorldLod::Detail&&lod!=WorldLod::Medium&&lod!=WorldLod::Far)throw std::invalid_argument("Invalid terrain LOD");
    const float x=cx*World::ChunkSize,z=cz*World::ChunkSize;
    bool wet=false;
    for(int iz=0;iz<16;++iz)for(int ix=0;ix<16;++ix) {
        const float px=x+ix*8,pz=z+iz*8;
        wet|=naturalHeight(px+4,pz+4)<WaterLevel+.5f;
        if(lod==WorldLod::Detail)groundPatch(mesh,world,px,pz,px+8,pz+8,terrainColor(world,px+4,pz+4),0,0,true);
    }
    if(lod==WorldLod::Detail)return wet;
    // Interior cells stay coarse. Only their outer edges receive the canonical 8 m
    // samples; a fan from a nonboundary corner stitches those samples without T-junctions.
    const int step=lod==WorldLod::Medium?2:4;
    std::array<uint32_t,17*17> lookup;lookup.fill(UINT32_MAX);
    auto vertex=[&](int ix,int iz) {
        auto& index=lookup[size_t(iz)*17+size_t(ix)];
        if(index==UINT32_MAX) {
            index=static_cast<uint32_t>(mesh.vertices.size());
            const float px=x+ix*8,pz=z+iz*8;
            mesh.vertices.push_back({{px,naturalHeight(px,pz),pz},terrainNormal(px,pz),terrainColor(world,px,pz),0});
        }
        return index;
    };
    for(int iz=0;iz<16;iz+=step)for(int ix=0;ix<16;ix+=step) {
        struct Point {int x,z;};
        std::array<Point,16> ring{};int count=0;
        const int left=ix==0?1:step,bottom=iz+step==16?1:step,right=ix+step==16?1:step,top=iz==0?1:step;
        for(int v=0;v<step;v+=left)ring[count++]={ix,iz+v};
        for(int u=0;u<step;u+=bottom)ring[count++]={ix+u,iz+step};
        for(int v=step;v>0;v-=right)ring[count++]={ix+step,iz+v};
        for(int u=step;u>0;u-=top)ring[count++]={ix+u,iz};
        int anchor=0;
        for(int i=0;i<count;++i)if(ring[i].x>0&&ring[i].x<16&&ring[i].z>0&&ring[i].z<16) {anchor=i;break;}
        const uint32_t a=vertex(ring[anchor].x,ring[anchor].z);
        for(int i=1;i<count-1;++i) {
            const auto b=ring[(anchor+i)%count],c=ring[(anchor+i+1)%count];
            mesh.indices.insert(mesh.indices.end(),{a,vertex(b.x,b.z),vertex(c.x,c.z)});
        }
    }
    return wet;
}
}

void addQuad(Mesh& m,Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 color,float material) {
    uint32_t n=static_cast<uint32_t>(m.vertices.size());Vec3 normal=normalized(cross(b-a,c-a));
    m.vertices.push_back({a,normal,color,material});m.vertices.push_back({b,normal,color,material});m.vertices.push_back({c,normal,color,material});m.vertices.push_back({d,normal,color,material});
    m.indices.insert(m.indices.end(),{n,n+1,n+2,n,n+2,n+3});
}
void addBox(Mesh& m,Vec3 center,Vec3 half,Vec3 color,float yaw,float material) {
    auto p=[&](float x,float y,float z){return center+rotated({x*half.x,y*half.y,z*half.z},yaw);};
    addQuad(m,p(-1,-1,1),p(1,-1,1),p(1,1,1),p(-1,1,1),color,material);
    addQuad(m,p(1,-1,-1),p(-1,-1,-1),p(-1,1,-1),p(1,1,-1),color,material);
    addQuad(m,p(1,-1,1),p(1,-1,-1),p(1,1,-1),p(1,1,1),color,material);
    addQuad(m,p(-1,-1,-1),p(-1,-1,1),p(-1,1,1),p(-1,1,-1),color,material);
    addQuad(m,p(-1,1,1),p(1,1,1),p(1,1,-1),p(-1,1,-1),color,material);
    addQuad(m,p(-1,-1,-1),p(1,-1,-1),p(1,-1,1),p(-1,-1,1),color,material);
}
void addCylinder(Mesh& m,Vec3 bottom,float radius,float height,Vec3 color,int sides,float material) {
    sides=std::max(3,sides);
    for(int i=0;i<sides;++i) {
        float a=2*Pi*i/sides,b=2*Pi*(i+1)/sides;
        Vec3 p=bottom+Vec3{std::cos(a)*radius,0,std::sin(a)*radius},q=bottom+Vec3{std::cos(b)*radius,0,std::sin(b)*radius};
        addQuad(m,q,p,p+Vec3{0,height,0},q+Vec3{0,height,0},color,material);
        tri(m,bottom+Vec3{0,height,0},q+Vec3{0,height,0},p+Vec3{0,height,0},color,material);
        tri(m,bottom,p,q,color,material);
    }
}
void appendMesh(Mesh& dst,const Mesh& src) {
    uint32_t base=static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.insert(dst.vertices.end(),src.vertices.begin(),src.vertices.end());
    dst.indices.reserve(dst.indices.size()+src.indices.size());
    for(uint32_t i:src.indices)dst.indices.push_back(base+i);
}
float World::height(float x,float z) const {
    if(garagePaving(x,z))return garageElevation(x,z);
    if(dockDeck(x,z))return lerp(naturalHeight(2600,z),.4f,clamp((x-2600)/24,0,1));
    if(causeway(x,z)) {
        float shoreX=coast(0)-160;
        return std::max(naturalHeight(x,z),lerp(naturalHeight(shoreX,0),5.2f,smooth(shoreX,shoreX+200,x)));
    }
    return naturalHeight(x,z);
}
float World::waterDepth(float x,float z) const {return std::max(0.0f,WaterLevel-naturalHeight(x,z));}
Biome World::biome(float x,float z) const {
    float md=mainlandDistance(x,z),id=islandDistance(x,z),d=std::max(md,id);
    if(d<25) return Biome::Ocean;
    if(d<150) return Biome::Beach;
    if(id>md) return Biome::Island;
    if(wetlands(x,z)) return Biome::Wetland;
    if(x>-1280 && x<1280 && z>-1280 && z<1408) return Biome::Downtown;
    if(x>-2816 && x<1900 && z>-2304 && z<2816) return Biome::Residential;
    return Biome::Countryside;
}
bool World::road(float x,float z) const {return axialRoad(x,z)||coastalRoad(x,z)||causeway(x,z)||(x>=-3200&&x<=-3072&&std::abs(z+1024)<5);}
GroundSurface World::groundSurface(float x,float z) const {
    if(!std::isfinite(x)||!std::isfinite(z)||std::abs(x)>Extent+512||std::abs(z)>Extent+512)return GroundSurface::Soil;
    if(garagePaving(x,z))return GroundSurface::Pavement;
    if(dockDeck(x,z))return GroundSurface::Wood;
    if(DockApproach.contains(x,z))return GroundSurface::Soil;
    for(const auto& area:AirfieldPaving)if(area.contains(x,z))return GroundSurface::Pavement;
    const int cx=int(std::floor(x/ChunkSize)),cz=int(std::floor(z/ChunkSize));
    const float bx=cx*ChunkSize,bz=cz*ChunkSize;
    const Biome center=biome(bx+64,bz+64);
    const bool urban=center==Biome::Downtown||center==Biome::Residential;
    for(int edge=0;edge<4;++edge) {
        const bool vertical=edge<2;
        const float line=(vertical?bx:bz)+(edge%2)*128;
        const float across=vertical?x:z,along=vertical?z:x,base=vertical?bz:bx;
        const float distance=(across-line)*(edge%2?-1.0f:1.0f);
        const float middle=base+std::floor((along-base)/16)*16+8;
        const float ax=vertical?line:middle,az=vertical?middle:line;
        if(distance<=RoadHalf&&(axialRoad(ax,az)||causeway(ax,az)))return GroundSurface::Pavement;
        if(urban&&distance>=10&&distance<=14&&along>=base+10&&along<=base+118&&
           axialRoad(vertical?line:bx+64,vertical?bz+64:line))return GroundSurface::Pavement;
    }
    if(urban) {
        if(plazaBlock(cx,cz)) {
            if(SurfaceArea{bx+15,bz+15,bx+113,bz+113}.contains(x,z))return GroundSurface::Pavement;
        } else if(center==Biome::Residential) {
            for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
                const float px=bx+36+i*56,pz=bz+36+j*56;
                if(!road(px,pz)&&biome(px,pz)!=Biome::Ocean&&residentialPath(px,pz,seedAt(cx*2+i,cz*2+j,71)).contains(x,z))return GroundSurface::Pavement;
            }
        }
    }
    // Coastal paving and base terrain are authored in eight-metre patches.
    const float sx=std::floor(x/8)*8+4,sz=std::floor(z/8)*8+4;
    if(coastalRoad(sx,sz))return GroundSurface::Pavement;
    const Biome natural=biome(sx,sz);
    if(natural==Biome::Wetland||natural==Biome::Ocean)return GroundSurface::Soil;
    if(natural==Biome::Beach)return GroundSurface::Sand;
    return GroundSurface::Grass;
}

Chunk World::generate(int cx,int cz,WorldLod lod) const {
    Chunk c;c.x=cx;c.z=cz;Mesh& m=c.mesh;
    if(lod==WorldLod::Detail) {m.vertices.reserve(10000);m.indices.reserve(16000);}
    float x=float(cx)*ChunkSize,z=float(cz)*ChunkSize;
    const auto block=worldGeometry::describeBlock(*this,cx,cz);
    const Biome center=block.biome;const uint32_t seed=block.seed;
    const bool wet=worldGeometry::appendTerrain(m,*this,cx,cz,lod);
    const size_t terrainIndices=m.indices.size();
    size_t coarseProjection=0;
    if(lod!=WorldLod::Detail)for(const auto& vertex:m.vertices)if(std::abs(vertex.position.y-m.vertices.front().position.y)>.0001f) {coarseProjection=terrainIndices;break;}
    // Preserve the detailed coastal paving mask at all LODs, including shoreline edges.
    for(int iz=0;iz<16;++iz)for(int ix=0;ix<16;++ix) {
        const float px=x+ix*8,pz=z+iz*8;
        if(coastalRoad(px+4,pz+4))groundPatch(m,*this,px,pz,px+8,pz+8,{.15f,.17f,.18f},.06f,4,false,coarseProjection);
    }
    if(wet) addQuad(m,{x,WaterLevel,z},{x,WaterLevel,z+128},{x+128,WaterLevel,z+128},{x+128,WaterLevel,z},{.08f,.36f,.42f},3);
    for(int edge=0;edge<4;++edge) {
        bool vertical=edge<2;float line=vertical?x+(edge%2)*128:z+(edge%2)*128;
        for(int s=0;s<8;++s) {
            float along=(vertical?z:x)+s*16,mid=along+8;
            float ax=vertical?line:mid,az=vertical?mid:line;
            if(!axialRoad(ax,az) && !causeway(ax,az))continue;
            bool perpendicular=gridDistance(vertical?az:ax,localRoadStep(ax,az))<RoadHalf;
            float lo=(edge%2==0)?0:-RoadHalf,hi=(edge%2==0)?RoadHalf:0;
            const size_t projection=causeway(ax,az)?terrainIndices:coarseProjection;
            if(vertical)groundPatch(m,*this,line+lo,along,line+hi,along+16,{.15f,.17f,.18f},.04f,4,false,projection);
            else groundPatch(m,*this,along,line+lo,along+16,line+hi,{.15f,.17f,.18f},.04f,4,false,projection);
            if(lod!=WorldLod::Detail || edge%2!=0 || perpendicular)continue;
            for(float lane:{-.20f,.20f}) {
                if(vertical)groundPatch(m,*this,line+lane-.06f,along+2,line+lane+.06f,along+12,{.88f,.69f,.24f},.055f,0,false,projection);
                else groundPatch(m,*this,along+2,line+lane-.06f,along+12,line+lane+.06f,{.88f,.69f,.24f},.055f,0,false,projection);
            }
            for(float lane:{-8.4f,8.4f}) {
                if(vertical)groundPatch(m,*this,line+lane-.08f,along+1,line+lane+.08f,along+15,{.78f,.80f,.74f},.057f,0,false,projection);
                else groundPatch(m,*this,along+1,line+lane-.08f,along+15,line+lane+.08f,{.78f,.80f,.74f},.057f,0,false,projection);
            }
        }
    }
    if(lod!=WorldLod::Detail) {
        if(block.plaza)landmarkPlaza(c,*this,x,z,block,lod);
        else for(const auto& spec:block.buildings) {
            building(c,spec,lod);
            if(block.urban&&spec.suburban) {
                const float px=spec.position.x+spec.halfWidth+5,pz=spec.position.z;
                coarseTree(m,{{px,height(px,pz),pz},.8f,spec.seed,worldGeometry::TreeKind::Broadleaf},lod);
            } else if(!block.urban) {
                const float px=spec.position.x+21,pz=spec.position.z;
                addCylinder(m,{px,height(px,pz),pz},4,11,{.58f,.61f,.58f},8,1);
            }
        }
        for(const auto& tree:worldGeometry::describeNaturalTrees(*this,cx,cz))coarseTree(m,tree,lod);
        if(block.urban)for(int edge=0;edge<4;++edge) {
            const bool vertical=edge<2;const float line=vertical?x+(edge%2)*128:z+(edge%2)*128;
            if(!axialRoad(vertical?line:x+64,vertical?z+64:line))continue;
            const float side=edge%2?-1.0f:1.0f,a=line+side*10,b=line+side*14;
            if(vertical)sidewalkPatch(m,*this,std::min(a,b),z+10,std::max(a,b),z+118,{.62f,.62f,.55f},.14f);
            else sidewalkPatch(m,*this,x+10,std::min(a,b),x+118,std::max(a,b),{.62f,.62f,.55f},.14f);
            for(float off:{29.0f,99.0f}) {
                Vec3 p=vertical?Vec3{line+side*16,0,z+off}:Vec3{x+off,0,line+side*16};p.y=height(p.x,p.z);
                coarseTree(m,{p,.86f,seed+uint32_t(edge*53+off),worldGeometry::TreeKind::Palm},lod);
            }
        }
    } else if((center==Biome::Downtown || center==Biome::Residential)) {
        for(int edge=0;edge<4;++edge) {
            bool vertical=edge<2;float line=vertical?x+(edge%2)*128:z+(edge%2)*128;
            if(!axialRoad(vertical?line:x+64,vertical?z+64:line))continue;
            float s=edge%2?-1.0f:1.0f;float a=line+s*10,b=line+s*14;
            if(vertical)sidewalkPatch(m,*this,std::min(a,b),z+10,std::max(a,b),z+118,{.62f,.62f,.55f},.14f);
            else sidewalkPatch(m,*this,x+10,std::min(a,b),x+118,std::max(a,b),{.62f,.62f,.55f},.14f);
            for(int tile=0;tile<13;++tile) {
                float t=(vertical?z:x)+12+tile*8;
                if(vertical)sidewalkPatch(m,*this,std::min(a,b),t,std::max(a,b),t+.045f,{.39f,.42f,.38f},.145f);
                else sidewalkPatch(m,*this,t,std::min(a,b),t+.045f,std::max(a,b),{.39f,.42f,.38f},.145f);
            }
        }
        // Zebra crossings are set back from every central city intersection.
        if(std::abs(cx)<=1&&std::abs(cz)<=1) {
            // Each chunk owns its half of every crossing; adjacent halves meet at the road centre.
            for(float corner:{16.5f,111.5f})for(int side=0;side<2;++side)for(int i=0;i<6;++i) {
                const float lo=side?128-i*1.5f-.8f:i*1.5f,hi=lo+.8f;
                groundPatch(m,*this,x+lo,z+corner-1.5f,x+hi,z+corner+1.5f,{.82f,.81f,.70f},.06f);
                groundPatch(m,*this,x+corner-1.5f,z+lo,x+corner+1.5f,z+hi,{.82f,.81f,.70f},.06f);
            }
        } else if(localRoadStep(x+64,z+64)==128)for(int i=0;i<6;++i) {
            groundPatch(m,*this,x+15,z+i*1.5f,x+18,z+i*1.5f+.8f,{.82f,.81f,.70f},.06f);
            groundPatch(m,*this,x+i*1.5f,z+15,x+i*1.5f+.8f,z+18,{.82f,.81f,.70f},.06f);
        }
        if(block.plaza)landmarkPlaza(c,*this,x,z,block);
        else for(const auto& spec:block.buildings) {
            const auto bs=spec.seed;const float px=spec.position.x,pz=spec.position.z,hx=spec.halfWidth,hz=spec.halfDepth;
            building(c,spec);
            if(spec.suburban) {
                const auto path=residentialPath(px,pz,bs);
                groundPatch(m,*this,path.x0,path.z0,path.x1,path.z1,{.55f,.54f,.45f},.06f);
                broadleaf(m,{px+hx+5,height(px+hx+5,pz),pz},.8f,bs);
                addBox(m,{px,height(px,pz+hz+5)+.6f,pz+hz+5},{hx+2,.6f,.5f},{.19f,.32f,.10f});
            }
        }
        for(int edge=0;edge<4;++edge) {
            bool vertical=edge<2;float line=vertical?x+(edge%2)*128:z+(edge%2)*128;
            if(!axialRoad(vertical?line:x+64,vertical?z+64:line))continue;
            float side=edge%2?-1.0f:1.0f;
            Vec3 p=vertical?Vec3{line+side*12,0,z+64}:Vec3{x+64,0,line+side*12};p.y=height(p.x,p.z)+.14f;
            for(float offset:{24.0f,64.0f,104.0f}) {
                // Leave the walking line at offset 12 clear, including the opening mission marker.
                Vec3 pole=vertical?Vec3{line+side*11,0,z+offset}:Vec3{x+offset,0,line+side*11};
                pole.y=height(pole.x,pole.z)+(garagePaving(pole.x,pole.z)?0.0f:.14f);
                streetlight(c,pole,vertical?(side>0?Pi:0):(side>0?Pi*.5f:-Pi*.5f));
            }
            if(edge%2==0)streetFurniture(c,p,vertical?0:-Pi*.5f,seed+static_cast<uint32_t>(edge)*13);
            for(float off:{29.0f,99.0f}) {
                Vec3 t=vertical?Vec3{line+side*16,0,z+off}:Vec3{x+off,0,line+side*16}; t.y=height(t.x,t.z);
                palm(m,t,.86f,seed+uint32_t(edge*53+off));
            }
        }
    } else {
        for(const auto& tree:worldGeometry::describeNaturalTrees(*this,cx,cz)) {
            using worldGeometry::TreeKind;
            const auto p=tree.position;
            if(tree.kind==TreeKind::Palm)palm(m,p,tree.scale,tree.seed);
            else if(tree.kind==TreeKind::Mangrove)mangrove(m,p,tree.scale,tree.seed);
            else if(tree.kind==TreeKind::Cypress)cypress(m,p,tree.scale,tree.seed);
            else broadleaf(m,p,tree.scale,tree.seed);
            const float radius=(tree.kind==TreeKind::Mangrove||tree.kind==TreeKind::Cypress)?.55f:.38f;
            c.solids.push_back({{p.x-radius,p.y,p.z-radius},{p.x+radius,p.y+4,p.z+radius}});
        }
        int coverCount=center==Biome::Wetland?36:48;
        for(int n=0;n<coverCount;++n) {
            uint32_t gs=seed+static_cast<uint32_t>(n)*503+19;
            float px=x+3+random01(gs)*122,pz=z+3+random01(gs+23)*122,h=height(px,pz);
            Biome local=biome(px,pz);
            if(road(px,pz)||siteReserve(px,pz)||local==Biome::Ocean||h<WaterLevel-.20f)continue;
            if(local==Biome::Wetland)grassTuft(m,{px,h,pz},.75f+random01(gs+47)*.6f,gs,true);
            else if(n%7==0)leafCrown(m,{px,h+.55f,pz},{1.2f,.8f,1.1f},{.21f,.34f,.11f},gs,6);
            else grassTuft(m,{px,h,pz},local==Biome::Beach?.8f:1.25f,gs,false);
        }
        for(const auto& spec:block.buildings) {
            building(c,spec);
            const float px=spec.position.x,pz=spec.position.z;
            addCylinder(m,{px+21,height(px+21,pz),pz},4,11,{.58f,.61f,.58f},12,1);
        }
    }
    if(cz==0 && x+128>coast(0)-160 && x<4540) {
        float rz=std::abs(z)<1?0:z+128;
        if(std::abs(rz)<.1f) {
            for(float zz:{-10.3f,10.3f})for(int segment=0;segment<8;++segment) {
                const float ax=std::max(x+segment*16,coast(0)-160),bx=std::min(x+(segment+1)*16,4540.0f);
                if(bx<=ax)continue;
                const Vec3 a{ax,height(ax,zz)+.62f,zz},b{bx,height(bx,zz)+.62f,zz};
                const Vec3 up{0,.45f,0},side{0,0,.18f};
                addQuad(m,a-up+side,b-up+side,b+up+side,a+up+side,{.57f,.62f,.61f});
                addQuad(m,b-up-side,a-up-side,a+up-side,b+up-side,{.57f,.62f,.61f});
                addQuad(m,a+up+side,b+up+side,b+up-side,a+up-side,{.57f,.62f,.61f});
                addQuad(m,a-up-side,b-up-side,b-up+side,a-up+side,{.57f,.62f,.61f});
            }
            for(float xx:{x+32,x+96}) {
                if(!causeway(xx,0))continue;
                const float h=height(xx,0);
                addCylinder(m,{xx,-20,0},1.7f,h+19.75f,{.43f,.49f,.48f},8);
                addBox(m,{xx,h-.6f,0},{3,.5f,10.6f},{.53f,.58f,.56f});
            }
        }
    }
    if(block.garage)worldGeometry::appendGarage(c,*this,lod);
    transportSites(c,*this,lod,coarseProjection);
    if(cx==24&&cz==8)clinicLaunch(c,lod);
    if(lod!=WorldLod::Detail) {m.vertices.shrink_to_fit();m.indices.shrink_to_fit();}
    return c;
}

Vec3 World::coastalRoadPoint(float fraction) {
    const float z=lerp(-4210.f,5500.f,clamp(fraction,0,1));
    return {coast(z)-185,0,z};
}
const char* World::district(Vec3 p) const {
    Biome b=biome(p.x,p.z);
    switch(b) {
    case Biome::Downtown:return p.x<0?"LANTERN QUARTER":"MERIDIAN EXCHANGE";
    case Biome::Residential:return p.z<-600?"PALM TERRACE":p.x<-900?"WESTHAVEN":"SUNWARD HEIGHTS";
    case Biome::Countryside:return p.z<-2500?"BREAKER LOWLANDS":"ALDER RIDGE";
    case Biome::Wetland:return "CYPRESS REACH";
    case Biome::Beach:return "EASTWIND STRAND";
    case Biome::Island:return "GLASSWATER KEYS";
    case Biome::Ocean:return "PELAGIC SOUND";
    }
    return "MERIDIAN COAST";
}
const std::vector<Landmark>& World::landmarks() {
    static const std::vector<Landmark> places={{{384,0,384},"Meridian Exchange"},{{-512,0,256},"Founders Gardens"},{{-640,0,128},"Lantern Quarter"},{{896,0,-512},"Palm Mile"},{{1024,0,-2816},"Cypress Reach"},{{-4096,0,2048},"Alder Ridge"},{{4096,World{}.height(4096,0),0},"Glasswater Causeway"},{{2304,0,768},"Eastwind Strand"},{{-2048,0,-2048},"Breaker Lowlands"},{{2674,.4f,768},"Glasswater Landing"},{{-3200,4,-1190},"Breaker Airfield"},{{3090,WaterLevel,1080},"Leena's Launch"},{World::garageSite().marker,"Harbor Motor Works"}};
    return places;
}
}
