#include "world.h"
#include <array>
#include <limits>
#include <utility>

namespace mc {
namespace {
constexpr float WaterLevel=-1.8f;
constexpr float RoadHalf=10.0f;
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
    return lerp(-2.8f,h,shore);
}
float gridDistance(float p,float step) { return std::abs(p-std::round(p/step)*step); }
float localRoadStep(float x,float z) {
    if(x>=-1664 && x<=1536 && z>=-1792 && z<=2048) return 128;
    if(x>=-3072 && x<=2048 && z>=-2560 && z<=3072) return 256;
    return 512;
}
bool causeway(float x,float z) { return x>coast(z)-160 && x<4540 && std::abs(z)<=RoadHalf+.5f; }
bool axialRoad(float x,float z) {
    if(mainlandDistance(x,z)<45 || std::abs(x)>World::Extent-30 || std::abs(z)>World::Extent-30) return false;
    float s=localRoadStep(x,z);
    return gridDistance(x,s)<RoadHalf || gridDistance(z,s)<RoadHalf;
}
bool coastalRoad(float x,float z) { return std::abs(x-(coast(z)-185))<8.0f && z>-4210 && z<5500; }
Vec3 rotated(Vec3 p,float yaw) {float s=std::sin(yaw),c=std::cos(yaw);return {p.x*c+p.z*s,p.y,-p.x*s+p.z*c};}
void tri(Mesh& m,Vec3 a,Vec3 b,Vec3 c,Vec3 color,float material=0) {
    uint32_t n=static_cast<uint32_t>(m.vertices.size()); Vec3 normal=normalized(cross(b-a,c-a));
    m.vertices.push_back({a,normal,color,material});m.vertices.push_back({b,normal,color,material});m.vertices.push_back({c,normal,color,material});
    m.indices.insert(m.indices.end(),{n,n+1,n+2});
}
void groundPatch(Mesh& m,const World& world,float x0,float z0,float x1,float z1,Vec3 color,float lift=0,float material=0,bool natural=false) {
    auto p=[&](float x,float z){return Vec3{x,(natural?naturalHeight(x,z):world.height(x,z))+lift,z};};
    addQuad(m,p(x0,z0),p(x0,z1),p(x1,z1),p(x1,z0),color,material);
}
void cone(Mesh& m,Vec3 bottom,float radius,float height,Vec3 color,int sides) {
    for(int i=0;i<sides;++i) {
        float a=2*Pi*float(i)/float(sides),b=2*Pi*float(i+1)/float(sides);
        tri(m,bottom+Vec3{radius*std::cos(b),0,radius*std::sin(b)},bottom+Vec3{radius*std::cos(a),0,radius*std::sin(a)},bottom+Vec3{0,height,0},color);
    }
}
void palm(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    float h=(8+random01(seed)*3)*scale;
    addCylinder(m,p,.22f*scale,h,{.40f,.29f,.18f},6);
    addCylinder(m,p+Vec3{0,h-.4f,0},.36f*scale,.75f*scale,{.26f,.32f,.11f},6);
    for(int f=0;f<7;++f) {
        float a=2*Pi*f/7+random01(seed+17)*2;
        Vec3 dir{std::cos(a),0,std::sin(a)},side{-dir.z,0,dir.x};
        Vec3 prev=p+Vec3{0,h,0};
        for(int j=0;j<3;++j) {
            float t=float(j+1)/3,old=float(j)/3;
            Vec3 next=p+Vec3{0,h+std::sin(t*Pi)*1.45f*scale-t*t*1.8f*scale,0}+dir*(t*4.4f*scale);
            float w0=(.12f+std::sin(old*Pi)*.5f)*scale,w1=(j==2?.012f:.12f+std::sin(t*Pi)*.5f)*scale;
            addQuad(m,prev-side*w0,next-side*w1,next+side*w1,prev+side*w0,{.09f+.025f*f,.25f+.016f*f,.08f});prev=next;
        }
    }
}
void broadleaf(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    addCylinder(m,p,.25f*scale,3.5f*scale,{.28f,.20f,.12f},5);
    for(int n=0;n<3;++n) {
        Vec3 q=p+Vec3{(random01(seed+n*11)-.5f)*2*scale,(2.7f+n*1.15f)*scale,(random01(seed+n*29)-.5f)*2*scale};
        cone(m,q,(2.7f-n*.35f)*scale,3.2f*scale,{.075f+n*.025f,.22f+n*.035f,.085f+n*.012f},7);
    }
}
void streetlight(Mesh& m,Vec3 p,float yaw) {
    addCylinder(m,p,.12f,8.4f,{.20f,.23f,.25f},6,1);
    addBox(m,p+rotated({1.0f,8.3f,0},yaw),{1.1f,.08f,.08f},{.22f,.25f,.27f},yaw,1);
    addBox(m,p+rotated({1.8f,8.24f,0},yaw),{.48f,.07f,.25f},{.94f,.85f,.60f},yaw,2);
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
void building(Chunk& chunk,Vec3 p,float hx,float hz,float h,uint32_t seed,bool suburban) {
    static constexpr Vec3 palette[]={{.77f,.68f,.53f},{.83f,.79f,.67f},{.60f,.66f,.66f},{.69f,.45f,.34f},{.77f,.72f,.62f},{.48f,.58f,.63f},{.88f,.77f,.61f},{.57f,.62f,.53f}};
    Vec3 c=palette[seed%8]; Mesh& m=chunk.mesh;
    addBox(m,p+Vec3{0,h*.5f,0},{hx,h*.5f,hz},c);
    chunk.solids.push_back({p+Vec3{-hx,0,-hz},p+Vec3{hx,h,hz}});
    addBox(m,p+Vec3{0,.3f,0},{hx+.25f,.3f,hz+.25f},c*.7f);
    windows(m,p,hx,hz,h,seed,!suburban);
    if(suburban) {
        roofGable(m,p+Vec3{0,h,0},hx+.7f,hz+.7f,2.6f,{.49f,.23f,.15f});
        addBox(m,p+Vec3{hx*.6f,h+1.5f,hz*.2f},{.8f,1.5f,.8f},c*.85f);
        addBox(m,p+Vec3{0,1.4f,-hz-.035f},{.8f,1.4f,.08f},{.20f,.29f,.28f});
        addBox(m,p+Vec3{0,2.9f,-hz-1.5f},{3,.16f,1.8f},{.84f,.77f,.61f});
        for(float dx:{-2.5f,2.5f})addBox(m,p+Vec3{dx,1.4f,-hz-2.7f},{.13f,1.4f,.13f},{.89f,.84f,.71f});
    } else {
        addBox(m,p+Vec3{0,h-.2f,0},{hx+.4f,.35f,hz+.4f},c*.80f);
        addBox(m,p+Vec3{0,h+.32f,0},{hx*.86f,.2f,hz*.86f},{.33f,.35f,.35f});
        addBox(m,p+Vec3{-hx*.35f,h+1.35f,hz*.2f},{2.5f,1,1.5f},{.51f,.54f,.54f},0,1);
        addBox(m,p+Vec3{hx*.4f,h+.8f,-hz*.35f},{1.3f,.6f,2.2f},{.40f,.45f,.45f},0,1);
        if(h>35) {
            addBox(m,p+Vec3{hx*.22f,h+3.0f,0},{hx*.35f,2.6f,hz*.4f},c*.83f);
            addCylinder(m,p+Vec3{hx*.22f,h+5.6f,0},.12f,5,{.67f,.67f,.61f},5,1);
        }
        for(int k=-1;k<=1;++k) {
            float x=float(k)*hx*.58f;
            addBox(m,p+Vec3{x,1.5f,-hz-.04f},{hx*.23f,1.35f,.05f},{.17f,.30f,.34f},0,2);
            addBox(m,p+Vec3{x,3.2f,-hz-.85f},{hx*.27f,.14f,1.1f},(seed%2)?Vec3{.20f,.45f,.39f}:Vec3{.69f,.28f,.17f});
        }
        for(float dx:{-hx+.2f,hx-.2f}) addBox(m,p+Vec3{dx,h*.5f,-hz-.06f},{.22f,h*.5f,.1f},c*.78f);
    }
}
void planter(Mesh& m,Vec3 p,float scale,uint32_t seed) {
    addBox(m,p+Vec3{0,.35f,0},{2.2f,.35f,2.2f},{.58f,.55f,.45f});
    addBox(m,p+Vec3{0,.73f,0},{1.9f,.04f,1.9f},{.20f,.28f,.10f});p.y+=.76f;palm(m,p,scale,seed);
}
void bench(Mesh& m,Vec3 p,float yaw) {
    addBox(m,p+Vec3{0,.58f,0},{1.3f,.10f,.40f},{.49f,.29f,.13f},yaw);
    addBox(m,p+rotated({0,.98f,.34f},yaw),{1.3f,.32f,.07f},{.49f,.29f,.13f},yaw);
    for(float a:{-.92f,.92f})addBox(m,p+rotated({a,.25f,0},yaw),{.08f,.25f,.32f},{.18f,.22f,.23f},yaw,1);
}
void landmarkPlaza(Chunk& c,const World& w,float x,float z,int kind) {
    Mesh& m=c.mesh;float h=w.height(x+64,z+64);
    groundPatch(m,w,x+15,z+15,x+113,z+113,{.63f,.63f,.56f},.05f);
    if(kind==0) {
        Vec3 p{x+64,h,z+64};
        building(c,p,23,25,72,0x71346u,false);
        building(c,p+Vec3{0,72,0},15,17,23,0x71516u,false);
        addBox(m,p+Vec3{0,101,0},{8,6,9},{.73f,.77f,.72f},0,1);
        addCylinder(m,p+Vec3{0,107,0},.32f,12,{.81f,.81f,.71f},8,1);
    } else {
        addCylinder(m,{x+64,h+.1f,z+64},12,.7f,{.62f,.63f,.56f},24);
        addCylinder(m,{x+64,h+.83f,z+64},10,.03f,{.14f,.49f,.58f},24,3);
        addCylinder(m,{x+64,h+.85f,z+64},2.0f,1.2f,{.67f,.63f,.51f},12);
        addBox(m,{x+64,h+4.5f,z+64},{.7f,3.0f,.7f},{.54f,.68f,.63f},.7f,1);
        addBox(m,{x+64,h+6.9f,z+64},{3.5f,.55f,.55f},{.54f,.68f,.63f},-.45f,1);
        c.solids.push_back({{x+62,h,z+62},{x+66,h+8,z+66}});
    }
    for(int k=0;k<4;++k) for(int j=0;j<2;++j) {
        float px=x+24+k*26,pz=z+(j?106:22);
        planter(m,{px,w.height(px,pz),pz},1.05f,seedAt(k,j,42));
    }
    for(int i=0;i<4;++i) bench(m,{x+26+i*25,h+.08f,z+41},0);
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
    if(causeway(x,z)) {
        float shoreX=coast(0)-160;
        return lerp(naturalHeight(shoreX,0),5.2f,smooth(shoreX,shoreX+200,x));
    }
    return naturalHeight(x,z);
}
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
bool World::road(float x,float z) const {return axialRoad(x,z)||coastalRoad(x,z)||causeway(x,z);}

Chunk World::generate(int cx,int cz) const {
    Chunk c;c.x=cx;c.z=cz;Mesh& m=c.mesh;
    m.vertices.reserve(10000);m.indices.reserve(16000);
    float x=float(cx)*ChunkSize,z=float(cz)*ChunkSize;
    Biome center=biome(x+64,z+64);uint32_t seed=seedAt(cx,cz);
    bool wet=false;
    for(int iz=0;iz<16;++iz) for(int ix=0;ix<16;++ix) {
        float px=x+ix*8,pz=z+iz*8,mx=px+4,mz=pz+4;
        Biome b=biome(mx,mz);float d=landDistance(mx,mz);
        Vec3 color{.24f,.34f,.16f};
        float shade=.92f+random01(seedAt(int(mx/8),int(mz/8)))*.13f;
        if(b==Biome::Downtown)color={.22f,.31f,.19f};
        if(b==Biome::Residential)color={.30f,.40f,.19f};
        if(b==Biome::Beach || d<45)color={.75f,.66f,.43f};
        if(b==Biome::Wetland)color={.24f,.29f,.13f};
        if(b==Biome::Ocean)color={.24f,.43f,.35f};
        if(b==Biome::Island)color={.27f,.39f,.17f};
        groundPatch(m,*this,px,pz,px+8,pz+8,color*shade,0,0,true);
        if(naturalHeight(mx,mz)<WaterLevel+.5f)wet=true;
        // The winding coastal road is sampled finely; the orthogonal street grid below has exact edges.
        if(coastalRoad(mx,mz))groundPatch(m,*this,px,pz,px+8,pz+8,{.15f,.17f,.18f},.06f,4);
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
            if(vertical)groundPatch(m,*this,line+lo,along,line+hi,along+16,{.15f,.17f,.18f},.04f,4);
            else groundPatch(m,*this,along,line+lo,along+16,line+hi,{.15f,.17f,.18f},.04f,4);
            if(edge%2!=0 || perpendicular)continue;
            for(float lane:{-.20f,.20f}) {
                if(vertical)groundPatch(m,*this,line+lane-.06f,along+2,line+lane+.06f,along+12,{.88f,.69f,.24f},.055f);
                else groundPatch(m,*this,along+2,line+lane-.06f,along+12,line+lane+.06f,{.88f,.69f,.24f},.055f);
            }
            for(float lane:{-8.4f,8.4f}) {
                if(vertical)groundPatch(m,*this,line+lane-.08f,along+1,line+lane+.08f,along+15,{.78f,.80f,.74f},.057f);
                else groundPatch(m,*this,along+1,line+lane-.08f,along+15,line+lane+.08f,{.78f,.80f,.74f},.057f);
            }
        }
    }
    if((center==Biome::Downtown || center==Biome::Residential)) {
        for(int edge=0;edge<4;++edge) {
            bool vertical=edge<2;float line=vertical?x+(edge%2)*128:z+(edge%2)*128;
            if(!axialRoad(vertical?line:x+64,vertical?z+64:line))continue;
            float s=edge%2?-1.0f:1.0f;float a=line+s*10,b=line+s*14;
            if(vertical)groundPatch(m,*this,std::min(a,b),z+10,std::max(a,b),z+118,{.62f,.62f,.55f},.14f);
            else groundPatch(m,*this,x+10,std::min(a,b),x+118,std::max(a,b),{.62f,.62f,.55f},.14f);
            for(int tile=0;tile<13;++tile) {
                float t=(vertical?z:x)+12+tile*8;
                if(vertical)groundPatch(m,*this,std::min(a,b),t,std::max(a,b),t+.045f,{.39f,.42f,.38f},.145f);
                else groundPatch(m,*this,t,std::min(a,b),t+.045f,std::max(a,b),{.39f,.42f,.38f},.145f);
            }
        }
        // Zebra crossings are set back from every central city intersection.
        if(localRoadStep(x+64,z+64)==128)for(int i=0;i<6;++i) {
            groundPatch(m,*this,x+15,z+i*1.5f,x+18,z+i*1.5f+.8f,{.82f,.81f,.70f},.06f);
            groundPatch(m,*this,x+i*1.5f,z+15,x+i*1.5f+.8f,z+18,{.82f,.81f,.70f},.06f);
        }
        bool landmark=(cx==3&&cz==3)||(cx==-4&&cz==2);
        bool park=!landmark && seed%17==0;
        if(landmark || park)landmarkPlaza(c,*this,x,z,(cx==3&&cz==3)?0:1);
        else for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
            uint32_t bs=seedAt(cx*2+i,cz*2+j,71);
            float px=x+36+i*56,pz=z+36+j*56;
            if(road(px,pz)||biome(px,pz)==Biome::Ocean)continue;
            bool sub=center==Biome::Residential;
            float hx=(sub?10.0f:14.0f)+random01(bs)*5,hz=(sub?11.0f:14.0f)+random01(bs+1)*6;
            float h=sub?5.9f+float(bs%3)*3.2f:12+random01(bs+2)*37;
            if(!sub && bs%7==0)h+=18;
            building(c,{px,height(px,pz),pz},hx,hz,h,bs,sub);
            if(sub) {
                groundPatch(m,*this,px-2,pz-hz-10,px+2,pz-hz,{.55f,.54f,.45f},.06f);
                broadleaf(m,{px+hx+5,height(px+hx+5,pz),pz},.8f,bs);
                addBox(m,{px,height(px,pz+hz+5)+.6f,pz+hz+5},{hx+2,.6f,.5f},{.19f,.32f,.10f});
            }
        }
        for(int edge=0;edge<4;++edge) {
            bool vertical=edge<2;float line=vertical?x+(edge%2)*128:z+(edge%2)*128;
            if(!axialRoad(vertical?line:x+64,vertical?z+64:line))continue;
            float side=edge%2?-1.0f:1.0f;
            Vec3 p=vertical?Vec3{line+side*12,0,z+64}:Vec3{x+64,0,line+side*12};p.y=height(p.x,p.z)+.14f;
            streetlight(m,p,vertical?(side>0?Pi:0):(side>0?Pi*.5f:-Pi*.5f));
            for(float off:{29.0f,99.0f}) {
                Vec3 t=vertical?Vec3{line+side*16,0,z+off}:Vec3{x+off,0,line+side*16}; t.y=height(t.x,t.z);
                palm(m,t,.86f,seed+uint32_t(edge*53+off));
            }
        }
    } else if(center!=Biome::Ocean) {
        int count=center==Biome::Wetland?23:center==Biome::Countryside?18:12;
        for(int n=0;n<count;++n) {
            uint32_t ts=seed+n*137u;float px=x+8+random01(ts)*112,pz=z+8+random01(ts+47)*112;
            float h=height(px,pz);if(road(px,pz)||h<WaterLevel+.15f||landDistance(px,pz)<40)continue;
            if(center==Biome::Beach || center==Biome::Island)palm(m,{px,h,pz},.8f+random01(ts+8)*.6f,ts);
            else broadleaf(m,{px,h,pz},.7f+random01(ts+8)*.8f,ts);
            c.solids.push_back({{px-.3f,h,pz-.3f},{px+.3f,h+4,pz+.3f}});
        }
        if(center==Biome::Countryside && seed%13==0) {
            float px=x+64,pz=z+64;
            if(!road(px,pz)) {
                building(c,{px,height(px,pz),pz},12,19,6.5f,seed,true);
                addCylinder(m,{px+21,height(px+21,pz),pz},4,11,{.58f,.61f,.58f},12,1);
            }
        }
    }
    if(cz==0 && causeway(x+64,0)) {
        float rz=std::abs(z)<1?0:z+128;
        if(std::abs(rz)<.1f) {
            float h=height(x+64,0);
            for(float zz:{-10.3f,10.3f})addBox(m,{x+64,h+.62f,zz},{64,.45f,.18f},{.57f,.62f,.61f});
            for(float xx:{x+32,x+96}) {
                addCylinder(m,{xx,-20,0},1.7f,h+19.75f,{.43f,.49f,.48f},8);
                addBox(m,{xx,h-.6f,0},{3,.5f,10.6f},{.53f,.58f,.56f});
            }
        }
    }
    return c;
}

bool World::stream(Vec3 position) {
    int x=int(std::floor(clamp(position.x,-Extent,Extent)/ChunkSize));
    int z=int(std::floor(clamp(position.z,-Extent,Extent)/ChunkSize));
    if(x==centerX && z==centerZ)return false;
    std::vector<Chunk> next;next.reserve((StreamRadius*2+1)*(StreamRadius*2+1));
    for(int iz=z-StreamRadius;iz<=z+StreamRadius;++iz)for(int ix=x-StreamRadius;ix<=x+StreamRadius;++ix) {
        bool found=false;
        for(auto& c:chunks)if(c.x==ix && c.z==iz) {next.push_back(std::move(c));c.x=0x7fffffff;found=true;break;}
        if(!found)next.push_back(generate(ix,iz));
    }
    chunks=std::move(next);centerX=x;centerZ=z;++revision;return true;
}
bool World::blocked(Vec3 p,float radius) const {
    radius=std::max(0.0f,radius);
    for(const auto& c:chunks)for(const auto& b:c.solids) {
        if(p.y>=b.max.y || p.y+1.7f<=b.min.y)continue;
        float x=clamp(p.x,b.min.x,b.max.x),z=clamp(p.z,b.min.z,b.max.z);
        float dx=p.x-x,dz=p.z-z;
        if(dx*dx+dz*dz<radius*radius || (p.x>b.min.x && p.x<b.max.x && p.z>b.min.z && p.z<b.max.z))return true;
    }
    return false;
}
Vec3 World::move(Vec3 from,Vec3 delta,float radius) const {
    radius=std::max(0.0f,radius);
    Vec3 p=from,remaining{delta.x,0,delta.z};
    // Recover safely if a saved position begins inside newly streamed collision geometry.
    for(int pass=0;pass<4;++pass) {
        bool moved=false;
        for(const auto& c:chunks)for(const auto& b:c.solids) {
            if(p.y>=b.max.y || p.y+1.7f<=b.min.y)continue;
            float loX=b.min.x-radius,hiX=b.max.x+radius,loZ=b.min.z-radius,hiZ=b.max.z+radius;
            if(p.x>loX && p.x<hiX && p.z>loZ && p.z<hiZ) {
                float distances[]={p.x-loX,hiX-p.x,p.z-loZ,hiZ-p.z};
                int side=0;for(int i=1;i<4;++i)if(distances[i]<distances[side])side=i;
                if(side==0)p.x=loX-.003f;else if(side==1)p.x=hiX+.003f;
                else if(side==2)p.z=loZ-.003f;else p.z=hiZ+.003f;
                moved=true;
            }
        }
        if(!moved)break;
    }
    // Continuous slab intersection prevents a fast vehicle crossing a thin wall in one update.
    for(int pass=0;pass<5;++pass) {
        float first=1;Vec3 normal{};bool hit=false;
        for(const auto& c:chunks)for(const auto& b:c.solids) {
            if(std::min(from.y,from.y+delta.y)>=b.max.y || std::max(from.y,from.y+delta.y)+1.7f<=b.min.y)continue;
            float enter=-std::numeric_limits<float>::infinity(),leave=1;Vec3 n{};bool valid=true;
            for(int axis=0;axis<2;++axis) {
                float pos=axis?p.z:p.x,d=axis?remaining.z:remaining.x;
                float lo=(axis?b.min.z:b.min.x)-radius,hi=(axis?b.max.z:b.max.x)+radius;
                if(std::abs(d)<1e-8f) {if(pos<=lo || pos>=hi){valid=false;break;}continue;}
                float near=(lo-pos)/d,far=(hi-pos)/d;float sign=-1;
                if(near>far){std::swap(near,far);sign=1;}
                if(near>enter) {enter=near;n=axis?Vec3{0,0,sign}:Vec3{sign,0,0};}
                leave=std::min(leave,far);
                if(enter>leave){valid=false;break;}
            }
            if(valid && enter>=-1e-6f && enter<first && leave>0 && dot(n,n)>0) {first=std::max(0.0f,enter);normal=n;hit=true;}
        }
        if(!hit){p+=remaining;break;}
        float distance=length(remaining);
        float safe=std::max(0.0f,first-.003f/std::max(distance,.003f));
        p+=remaining*safe;remaining=remaining*(1-first);
        float into=dot(remaining,normal);if(into<0)remaining-=normal*into;
        if(dot(remaining,remaining)<1e-8f)break;
    }
    p.y=from.y+delta.y;
    p.x=clamp(p.x,-Extent+1,Extent-1);p.z=clamp(p.z,-Extent+1,Extent-1);return p;
}
Mesh World::combinedMesh() const {
    Mesh m;size_t v=0,i=0;for(const auto& c:chunks){v+=c.mesh.vertices.size();i+=c.mesh.indices.size();}
    m.vertices.reserve(v);m.indices.reserve(i);for(const auto& c:chunks)appendMesh(m,c.mesh);return m;
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
    static const std::vector<Landmark> places={{{384,0,384},"Meridian Exchange"},{{-512,0,256},"Founders Gardens"},{{-640,0,128},"Lantern Quarter"},{{896,0,-512},"Palm Mile"},{{1024,0,-2816},"Cypress Reach"},{{-4096,0,2048},"Alder Ridge"},{{4096,5.2f,0},"Glasswater Causeway"},{{2304,0,768},"Eastwind Strand"},{{-2048,0,-2048},"Breaker Lowlands"}};
    return places;
}
}
