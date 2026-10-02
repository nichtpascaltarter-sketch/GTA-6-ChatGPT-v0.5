#include "world.h"
#include <array>
#include <limits>
#include <utility>

namespace mc {
namespace {
constexpr float WaterLevel=World::WaterLevel;
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
void signText(Mesh& m,Vec3 center,Vec3 along,const char* label,float scale,Vec3 color) {
    // Thirteen original monoline strokes form a compact, geometry-only shop alphabet.
    static constexpr float strokes[13][4]={{0,6,4,6},{4,6,4,3},{4,3,4,0},{4,0,0,0},{0,0,0,3},{0,3,0,6},{0,3,4,3},{0,6,2,3},{4,6,2,3},{0,0,2,3},{4,0,2,3},{2,6,2,3},{2,3,2,0}};
    int count=0;for(const char* p=label;*p;++p)++count;
    Vec3 origin=center-along*((count*5.3f-1.3f)*scale*.5f);
    for(const char* p=label;*p;++p,origin+=along*(5.3f*scale))for(const char* s=letterStrokes(*p);*s;++s) {
        const float* a=strokes[*s-'a'];facadeLine(m,origin,along,a[0]*scale,a[1]*scale,a[2]*scale,a[3]*scale,.31f*scale,color,2);
    }
}
void shopfront(Chunk& chunk,Vec3 p,float width,float yaw,uint32_t seed,bool illuminated) {
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
    signText(m,p+Vec3{0,3.46f,0}+out*.37f,along,names[seed%8],std::min(.15f,width*.033f),{.95f,.85f,.59f});
    for(int k=0;k<12;++k) {
        float a=-width+k*width/6,b=a+width/6;
        Vec3 color=k%2?awning:Vec3{.81f,.76f,.59f};
        addQuad(m,p+along*a+Vec3{0,3.2f,0}+out*.25f,p+along*b+Vec3{0,3.2f,0}+out*.25f,p+along*b+Vec3{0,2.85f,0}+out*1.8f,p+along*a+Vec3{0,2.85f,0}+out*1.8f,color);
        addQuad(m,p+along*a+Vec3{0,2.85f,0}+out*1.8f,p+along*b+Vec3{0,2.85f,0}+out*1.8f,p+along*b+Vec3{0,2.63f,0}+out*1.8f,p+along*a+Vec3{0,2.63f,0}+out*1.8f,color);
    }
    addBox(m,p+along*(width*.65f)+Vec3{0,1.38f,0}+out*.3f,{.83f,1.38f,.06f},{.09f,.17f,.18f},yaw,2);
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
void building(Chunk& chunk,Vec3 p,float hx,float hz,float h,uint32_t seed,bool suburban) {
    static constexpr Vec3 palette[]={{.77f,.68f,.53f},{.83f,.79f,.67f},{.60f,.66f,.66f},{.69f,.45f,.34f},{.77f,.72f,.62f},{.48f,.58f,.63f},{.88f,.77f,.61f},{.57f,.62f,.53f}};
    Vec3 c=palette[seed%8];Mesh& m=chunk.mesh;int style=int(seed%6);
    if(suburban) {
        addBox(m,p+Vec3{0,h*.5f,0},{hx,h*.5f,hz},c);windows(m,p,hx,hz,h,seed,false);
        roofGable(m,p+Vec3{0,h,0},hx+.7f,hz+.7f,2.6f,{.49f,.23f,.15f});
        addBox(m,p+Vec3{hx*.6f,h+1.5f,hz*.2f},{.8f,1.5f,.8f},c*.85f);
        addBox(m,p+Vec3{0,1.4f,-hz-.035f},{.8f,1.4f,.08f},{.20f,.29f,.28f});
        addBox(m,p+Vec3{0,2.9f,-hz-1.5f},{3,.16f,1.8f},{.84f,.77f,.61f});
        for(float dx:{-2.5f,2.5f})addBox(m,p+Vec3{dx,1.4f,-hz-2.7f},{.13f,1.4f,.13f},{.89f,.84f,.71f});
    } else {
        if(style==2)h=std::min(h,22.5f);
        if(style==5)h=std::min(h,28.0f);
        if(style==0)c={.77f,.53f,.38f};
        if(style==1)c={.42f,.53f,.57f};
        if(style==2)c={.49f,.28f,.20f};
        if(style==3)c={.87f,.80f,.66f};
        if(style==4)c={.64f,.70f,.66f};
        if(style==5)c={.75f,.72f,.61f};
        constexpr float podium=4.7f;
        addBox(m,p+Vec3{0,podium*.5f,0},{hx,podium*.5f,hz},c*.82f);
        addBox(m,p+Vec3{0,podium,0},{hx+.45f,.18f,hz+.45f},c*1.08f);
        if(style==4 && h>20) {
            float radius=std::min(hx,hz)*.96f;
            addCylinder(m,p+Vec3{0,podium,0},radius,h-podium,c,20);
            roundWindows(m,p+Vec3{0,podium,0},radius+.045f,h-podium,std::max(3,int((h-podium)/3.5f)));
            addCylinder(m,p+Vec3{0,h,0},radius+1,.35f,c*.72f,20);
            addCylinder(m,p+Vec3{0,h+.35f,0},radius*.66f,3.0f,{.28f,.40f,.39f},20,1);
            cone(m,p+Vec3{0,h+3.35f,0},radius*.75f,4.5f,{.34f,.49f,.46f},20);
        } else {
            float bx=hx,bz=hz,base=podium;int tiers=(style==0||style==1)?3:1;
            for(int tier=0;tier<tiers;++tier) {
                float part=(h-podium)*(tiers==1?1.0f:tier==0?.62f:.19f);
                addBox(m,p+Vec3{0,base+part*.5f,0},{bx,part*.5f,bz},c*(1.0f-tier*.04f));
                windows(m,p+Vec3{0,base-.4f,0},bx,bz,part+.2f,seed+uint32_t(tier)*71,true);
                addBox(m,p+Vec3{0,base+part-.08f,0},{bx+.3f,.17f,bz+.3f},c*.76f);
                base+=part;bx*=.78f;bz*=.78f;
            }
            if(style==0) {
                for(float dx:{-.70f,-.35f,.35f,.70f})addBox(m,p+Vec3{dx*hx,podium+(h-podium)*.30f,-hz-.10f},{.24f,(h-podium)*.30f,.16f},{.87f,.73f,.52f});
                addBox(m,p+Vec3{0,h+2.8f,0},{hx*.29f,2.8f,hz*.29f},c*.88f);
                cone(m,p+Vec3{0,h+5.6f,0},3,4,{.33f,.49f,.45f},4);
            } else if(style==1) {
                addBox(m,p+Vec3{0,h+2.2f,0},{hx*.48f,2.2f,hz*.4f},{.18f,.28f,.32f},0,2);
                for(float dx:{-.8f,.8f})addCylinder(m,p+Vec3{dx*hx*.5f,h+4.4f,0},.17f,7,{.60f,.66f,.65f},5,1);
            } else if(style==2) {
                for(int roof=0;roof<3;++roof)roofGable(m,p+Vec3{-hx+(roof+.5f)*hx*2/3,h,0},hx/3+.18f,hz+.5f,3.2f,{.29f,.32f,.30f});
                for(int row=1;row<int(h/3.5f);++row)addBox(m,p+Vec3{0,row*3.5f,-hz-.045f},{hx,.06f,.05f},{.63f,.43f,.30f});
                addCylinder(m,p+Vec3{hx*.72f,h-1,hz*.55f},1.25f,7,{.47f,.28f,.20f},8);
            } else if(style==3) {
                for(float y=8;y<h-1;y+=5.5f) {
                    addBox(m,p+Vec3{0,y,-hz-.65f},{hx+.5f,.12f,.85f},c*.92f);
                    addBox(m,p+Vec3{0,y+.8f,-hz-1.4f},{hx+.4f,.10f,.045f},{.26f,.36f,.36f},0,1);
                    for(int k=-3;k<=3;++k)addBox(m,p+Vec3{k*hx/3.2f,y+.42f,-hz-1.4f},{.04f,.42f,.045f},{.26f,.36f,.36f},0,1);
                }
                addBox(m,p+Vec3{0,h+1,0},{hx+.7f,.18f,hz+.7f},c);
                for(float dx:{-.85f,.85f})for(float dz:{-.85f,.85f})addBox(m,p+Vec3{dx*hx,h+.5f,dz*hz},{.12f,.5f,.12f},c);
            } else {
                roofGable(m,p+Vec3{0,h,0},hx+.6f,hz+.6f,6.5f,{.24f,.33f,.32f});
                for(float dx:{-.7f,0.0f,.7f})addBox(m,p+Vec3{dx*hx,podium+(h-podium)*.5f,-hz-.20f},{.42f,(h-podium)*.5f,.3f},c*1.1f);
            }
        }
        shopfront(chunk,p+Vec3{0,0,-hz-.04f},hx*.86f,0,seed,seed%3==0);
        shopfront(chunk,p+Vec3{-hx-.04f,0,0},hz*.86f,Pi*.5f,seed+3,seed%5==0);
        addBox(m,p+Vec3{-hx*.32f,h+1.0f,hz*.2f},{2.3f,.8f,1.45f},{.45f,.48f,.47f},0,1);
    }
    chunk.solids.push_back({p+Vec3{-hx,0,-hz},p+Vec3{hx,h,hz}});
    addBox(m,p+Vec3{0,.23f,0},{hx+.18f,.23f,hz+.18f},c*.67f);
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
        Vec3 shelter=p+across*4+along*21;
        for(float a:{-2.8f,2.8f})for(float b:{-.85f,.85f})addBox(m,shelter+along*a+across*b+Vec3{0,1.5f,0},{.065f,1.5f,.065f},{.23f,.32f,.33f},yaw,1);
        addBox(m,shelter+Vec3{0,3.0f,0},{1.2f,.14f,3.25f},{.18f,.29f,.30f},yaw);
        addBox(m,shelter+across*.90f+Vec3{0,1.65f,0},{.04f,1.12f,2.75f},{.20f,.36f,.39f},yaw,2);
        bench(m,shelter-across*.1f+Vec3{0,.02f,0},yaw+Pi*.5f);
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
void clockPavilion(Chunk& chunk,Vec3 p) {
    Mesh& m=chunk.mesh;
    addBox(m,p+Vec3{0,1.5f,0},{4.6f,1.5f,4.6f},{.56f,.51f,.38f});
    addBox(m,p+Vec3{0,10.6f,0},{3.6f,7.6f,3.6f},{.76f,.69f,.51f});
    for(float y:{3.2f,15.8f,18.2f})addBox(m,p+Vec3{0,y,0},{4.0f,.22f,4.0f},{.88f,.80f,.58f});
    addCylinder(m,p+Vec3{0,18.4f,0},4.7f,1,{.30f,.43f,.39f},8,1);
    cone(m,p+Vec3{0,19.4f,0},5.4f,5.8f,{.28f,.45f,.39f},8);
    addCylinder(m,p+Vec3{0,25.2f,0},.14f,2.5f,{.68f,.66f,.48f},6,1);
    for(int face=0;face<4;++face) {
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
    chunk.solids.push_back({p-Vec3{4.6f,0,4.6f},p+Vec3{4.6f,26,4.6f}});
}
void marketArcade(Chunk& chunk,Vec3 p) {
    Mesh& m=chunk.mesh;
    for(int shop=0;shop<3;++shop) {
        Vec3 q=p+Vec3{0,0,shop*19.0f};
        addBox(m,q+Vec3{0,2.5f,0},{7,2.5f,7.7f},{.73f,.63f,.43f});
        shopfront(chunk,q+Vec3{-7.05f,0,0},6.8f,Pi*.5f,static_cast<uint32_t>(shop*3+2),true);
        roofGable(m,q+Vec3{0,5,0},7.6f,8.3f,2.6f,{.48f,.24f,.13f});
        addBox(m,q+Vec3{-8.1f,1.0f,8.2f},{1.7f,1,1.4f},{.22f,.34f,.20f});
        for(int crate=0;crate<3;++crate) {
            Vec3 r=q+Vec3{-9.2f,1.45f,float(crate)*.85f-1};
            addBox(m,r,{.62f,.25f,.35f},{.53f,.35f,.17f});
            for(int fruit=0;fruit<4;++fruit)addCylinder(m,r+Vec3{-.40f+fruit*.27f,.26f,0},.14f,.21f,shop==0?Vec3{.85f,.52f,.10f}:Vec3{.61f,.19f,.08f},6);
        }
        chunk.solids.push_back({q-Vec3{7,0,7.7f},q+Vec3{7,6,7.7f}});
    }
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
    if(c.x==0 && c.z==0) {
        clockPavilion(c,{x+44,h,z+98});
        marketArcade(c,{x+96,h,z+35});
        for(float px:{20.0f,31.0f,42.0f,53.0f}) {
            addCylinder(m,{x+px,h,z+18},.15f,.90f,{.21f,.28f,.26f},8,1);
            addCylinder(m,{x+px,h+.64f,z+18},.17f,.09f,{.79f,.69f,.42f},8,1);
        }
    }
    for(int k=0;k<4;++k) for(int j=0;j<2;++j) {
        float px=x+24+k*26,pz=z+(j?106:22);
        planter(m,{px,w.height(px,pz),pz},1.05f,seedAt(k,j,42));
    }
    for(int i=0;i<4;++i) bench(m,{x+26+i*25,h+.08f,z+41},0);
}
void transportSites(Chunk& c,const World& world) {
    float x0=c.x*World::ChunkSize,z0=c.z*World::ChunkSize,x1=x0+World::ChunkSize,z1=z0+World::ChunkSize;
    auto owns=[&](float x,float z){return x>=x0&&x<x1&&z>=z0&&z<z1;};
    auto patch=[&](float ax,float az,float bx,float bz,Vec3 color,float lift=0,float material=0) {
        ax=std::max(ax,x0);az=std::max(az,z0);bx=std::min(bx,x1);bz=std::min(bz,z1);
        if(bx>ax&&bz>az)groundPatch(c.mesh,world,ax,az,bx,bz,color,lift,material);
    };
    Mesh& m=c.mesh;
    if(x1>-3320&&x0<-3050&&z1>-1430&&z0<-550) {
        patch(-3218,-1256,-3182,-744,{.13f,.16f,.17f},.025f,4);
        patch(-3288,-1058,-3182,-994,{.20f,.22f,.21f},.025f,4);
        patch(-3182,-1029,-3072,-1019,{.20f,.22f,.21f},.025f,4);
        for(float x:{-3216.0f,-3184.0f})patch(x-.10f,-1248,x+.10f,-752,{.82f,.82f,.68f},.038f);
        for(int stripe=0;stripe<15;++stripe) {
            float z=-1232+stripe*32.0f;patch(-3200.17f,z,-3199.83f,z+12,{.84f,.83f,.72f},.04f);
        }
        for(int bar=0;bar<8;++bar)for(float z:{-1248.0f,-764.0f}) {
            float x=-3213.5f+bar*3.7f;patch(x,z,x+1.3f,z+12,{.85f,.83f,.72f},.04f);
        }
        for(float x:{-3213.0f,-3193.0f})for(float z:{-1200.0f,-812.0f})patch(x,z,x+6,z+12,{.84f,.83f,.72f},.04f);
        for(float x:{-3220.0f,-3180.0f})for(int lamp=0;lamp<9;++lamp) {
            float z=-1256+lamp*64.0f;if(!owns(x,z))continue;
            addCylinder(m,{x,4.02f,z},.19f,.18f,{.48f,.47f,.32f},6);
            addCylinder(m,{x,4.20f,z},.12f,.11f,{.95f,.77f,.33f},6,2);
        }
        if(owns(-3268,-1024)) {
            Vec3 p{-3268,4,-1024};
            addBox(m,p+Vec3{0,5,0},{20,5,27},{.48f,.53f,.47f});
            roofGable(m,p+Vec3{0,10,0},20.7f,27.7f,4.5f,{.33f,.40f,.37f});
            addBox(m,p+Vec3{20.06f,4,0},{.07f,4,19},{.22f,.29f,.28f});
            for(int rail=1;rail<8;++rail)addBox(m,p+Vec3{20.16f,rail*.95f,0},{.04f,.04f,19},{.53f,.58f,.51f});
            signText(m,p+Vec3{20.2f,8.45f,0},{0,0,1},"BREAKER AIR",.19f,{.93f,.86f,.63f});
            c.solids.push_back({p-Vec3{20,0,27},p+Vec3{20,11,27}});
        }
        if(owns(-3231,-1236)) {
            Vec3 p{-3231,4,-1236};addCylinder(m,p,.11f,6.4f,{.55f,.57f,.48f},6,1);
            for(int band=0;band<5;++band) {
                float a=band/5.0f,b=(band+1)/5.0f;
                branch(m,p+Vec3{a*2.8f,6.2f-a*.4f,0},p+Vec3{b*2.8f,6.2f-b*.4f,0},.36f-a*.22f,.36f-b*.22f,band%2?Vec3{.88f,.80f,.60f}:Vec3{.83f,.29f,.10f},8);
            }
            c.solids.push_back({p-Vec3{.12f,0,.12f},p+Vec3{.12f,6.4f,.12f}});
        }
    }
    if(x1>2494&&x0<2690&&z1>740&&z0<795) {
        patch(2504,765,2600,771,{.58f,.54f,.39f},.035f);
        for(int board=0;board<34;++board) {
            float x=2600+board*2.0f;patch(x,765,std::min(x+1.97f,2667.0f),771,board%3?Vec3{.48f,.35f,.20f}:Vec3{.55f,.41f,.24f});
        }
        for(int board=0;board<14;++board) {
            float z=754+board*2.0f;patch(2667,z,2675,z+1.97f,board%3?Vec3{.48f,.35f,.20f}:Vec3{.55f,.41f,.24f});
        }
        for(float z:{765.3f,770.7f})for(int piling=0;piling<7;++piling) {
            float x=2624+piling*8.0f;if(!owns(x,z))continue;
            float bottom=naturalHeight(x,z)-.8f;
            addCylinder(m,{x,bottom,z},.24f,.32f-bottom,{.31f,.25f,.17f},7);
            addBox(m,{x,.18f,768},{.18f,.17f,3.1f},{.38f,.28f,.16f});
            c.solids.push_back({{x-.24f,bottom,z-.24f},{x+.24f,.32f,z+.24f}});
        }
        auto rail=[&](Vec3 a,Vec3 b) {
            Vec3 mid=(a+b)*.5f;if(!owns(mid.x,mid.z))return;
            Vec3 half{std::max(.065f,std::abs(b.x-a.x)*.5f),.07f,std::max(.065f,std::abs(b.z-a.z)*.5f)};
            addBox(m,mid+Vec3{0,.85f,0},half,{.54f,.41f,.25f});
            for(Vec3 p:{a,b})addBox(m,p+Vec3{0,.47f,0},{.085f,.47f,.085f},{.43f,.31f,.19f});
            c.solids.push_back({mid-Vec3{half.x,0,half.z},mid+Vec3{half.x,.92f,half.z}});
        };
        for(float z:{765.0f,771.0f})for(int section=0;section<5;++section)rail({2624+section*8.6f,.4f,z},{2624+(section+1)*8.6f,.4f,z});
        rail({2667,.4f,754},{2675,.4f,754});rail({2667,.4f,782},{2675,.4f,782});
        rail({2667,.4f,754},{2667,.4f,765});rail({2667,.4f,771},{2667,.4f,782});
        rail({2675,.4f,754},{2675,.4f,764});rail({2675,.4f,772},{2675,.4f,782});
        for(float z:{755.0f,781.0f})if(owns(2668,z)) {
            Vec3 p{2668,.4f,z};addCylinder(m,p,.10f,4,{.25f,.31f,.29f},6,1);
            addBox(m,p+Vec3{0,4,0},{.38f,.10f,.38f},{1,.76f,.43f},0,2);
            c.lights.push_back({p+Vec3{0,3.9f,0},20,{1,.76f,.43f},55,{0,-1,0},-.15f});
            c.solids.push_back({p-Vec3{.1f,0,.1f},p+Vec3{.1f,4,.1f}});
        }
    }
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
    if(dockDeck(x,z))return lerp(naturalHeight(2600,z),.4f,clamp((x-2600)/24,0,1));
    if(causeway(x,z)) {
        float shoreX=coast(0)-160;
        return lerp(naturalHeight(shoreX,0),5.2f,smooth(shoreX,shoreX+200,x));
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
            for(float offset:{24.0f,64.0f,104.0f}) {
                // Leave the walking line at offset 12 clear, including the opening mission marker.
                Vec3 pole=vertical?Vec3{line+side*11,0,z+offset}:Vec3{x+offset,0,line+side*11};
                pole.y=height(pole.x,pole.z)+.14f;
                streetlight(c,pole,vertical?(side>0?Pi:0):(side>0?Pi*.5f:-Pi*.5f));
            }
            if(edge%2==0)streetFurniture(c,p,vertical?0:-Pi*.5f,seed+static_cast<uint32_t>(edge)*13);
            for(float off:{29.0f,99.0f}) {
                Vec3 t=vertical?Vec3{line+side*16,0,z+off}:Vec3{x+off,0,line+side*16}; t.y=height(t.x,t.z);
                palm(m,t,.86f,seed+uint32_t(edge*53+off));
            }
        }
    } else {
        int count=center==Biome::Wetland?18:center==Biome::Countryside?18:center==Biome::Ocean?5:14;
        for(int n=0;n<count;++n) {
            uint32_t ts=seed+n*137u;float px=x+8+random01(ts)*112,pz=z+8+random01(ts+47)*112;
            float h=height(px,pz);Biome local=biome(px,pz);
            if(road(px,pz)||siteReserve(px,pz)||h<WaterLevel-(local==Biome::Wetland?.5f:-.15f)||landDistance(px,pz)<40)continue;
            float scale=.7f+random01(ts+8)*.8f;
            if(local==Biome::Beach || local==Biome::Island)palm(m,{px,h,pz},scale,ts);
            else if(local==Biome::Wetland) {
                if(h<WaterLevel+.7f)mangrove(m,{px,h,pz},scale,ts);else cypress(m,{px,h,pz},scale,ts);
            } else if(local!=Biome::Ocean)broadleaf(m,{px,h,pz},scale,ts);
            else continue;
            float radius=local==Biome::Wetland?.55f:.38f;
            c.solids.push_back({{px-radius,h,pz-radius},{px+radius,h+4,pz+radius}});
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
        if(center==Biome::Countryside && seed%13==0) {
            float px=x+64,pz=z+64;
            if(!road(px,pz)&&!siteReserve(px,pz)) {
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
    transportSites(c,*this);
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
    static const std::vector<Landmark> places={{{384,0,384},"Meridian Exchange"},{{-512,0,256},"Founders Gardens"},{{-640,0,128},"Lantern Quarter"},{{896,0,-512},"Palm Mile"},{{1024,0,-2816},"Cypress Reach"},{{-4096,0,2048},"Alder Ridge"},{{4096,5.2f,0},"Glasswater Causeway"},{{2304,0,768},"Eastwind Strand"},{{-2048,0,-2048},"Breaker Lowlands"},{{2674,.4f,768},"Glasswater Landing"},{{-3200,4,-1190},"Breaker Airfield"}};
    return places;
}
}
