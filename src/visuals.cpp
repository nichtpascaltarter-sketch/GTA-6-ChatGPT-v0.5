#include "game.h"
#include <array>

namespace mc {
namespace {
constexpr Vec3 Garage{140,0,128};
constexpr Vec3 Outfitter{-116,0,128};
float planarDistance(Vec3 a,Vec3 b){a.y=b.y=0;return length(a-b);}
Vec3 rotate(Vec3 p,float yaw){return right(yaw)*p.x+Vec3{0,p.y,0}+forward(yaw)*p.z;}
void triangle(Mesh& mesh,Vec3 a,Vec3 b,Vec3 c,Vec3 color,float material=0){
    const Vec3 normal=normalized(cross(b-a,c-a));
    if(length(normal)<.5f)return;
    const uint32_t index=uint32_t(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),{{a,normal,color,material},{b,normal,color,material},{c,normal,color,material}});
    mesh.indices.insert(mesh.indices.end(),{index,index+1,index+2});
}
void ellipsoid(Mesh& mesh,Vec3 center,Vec3 radii,float yaw,Vec3 color,int segments,int rings,float material=0){
    segments=std::max(5,segments);rings=std::max(3,rings);
    const uint32_t start=uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({center+Vec3{0,-radii.y,0},{0,-1,0},color,material});
    for(int j=1;j<rings;++j){
        const float latitude=-Pi*.5f+Pi*float(j)/float(rings),radial=std::cos(latitude),height=std::sin(latitude);
        for(int i=0;i<segments;++i){
            const float theta=2*Pi*float(i)/float(segments);Vec3 unit{std::sin(theta)*radial,height,std::cos(theta)*radial};
            Vec3 offset{unit.x*radii.x,unit.y*radii.y,unit.z*radii.z};
            Vec3 normal=normalized(Vec3{unit.x/radii.x,unit.y/radii.y,unit.z/radii.z});
            mesh.vertices.push_back({center+rotate(offset,yaw),rotate(normal,yaw),color,material});
        }
    }
    const uint32_t north=uint32_t(mesh.vertices.size());mesh.vertices.push_back({center+Vec3{0,radii.y,0},{0,1,0},color,material});
    for(int i=0;i<segments;++i){
        const uint32_t a=start+1+uint32_t(i),b=start+1+uint32_t((i+1)%segments);
        mesh.indices.insert(mesh.indices.end(),{start,b,a});
        for(int j=0;j<rings-2;++j){uint32_t lo=a+uint32_t(j*segments),next=b+uint32_t(j*segments),hi=lo+uint32_t(segments),hiNext=next+uint32_t(segments);mesh.indices.insert(mesh.indices.end(),{lo,next,hiNext,lo,hiNext,hi});}
        const uint32_t last=a+uint32_t((rings-2)*segments),lastNext=b+uint32_t((rings-2)*segments);mesh.indices.insert(mesh.indices.end(),{north,last,lastNext});
    }
}
void tube(Mesh& mesh,Vec3 a,Vec3 b,float radiusA,float radiusB,Vec3 color,int sides,float material=0){
    Vec3 axis=b-a;float distance=length(axis);if(distance<.0001f)return;axis=axis/distance;
    Vec3 side=normalized(cross(axis,std::abs(axis.z)<.8f?Vec3{0,0,1}:Vec3{1,0,0})),fore=normalized(cross(side,axis));
    const uint32_t start=uint32_t(mesh.vertices.size());
    for(int end=0;end<2;++end)for(int i=0;i<sides;++i){float angle=2*Pi*float(i)/float(sides);Vec3 radial=side*std::cos(angle)+fore*std::sin(angle);Vec3 normal=normalized(radial+axis*((radiusA-radiusB)/distance));mesh.vertices.push_back({(end?b:a)+radial*(end?radiusB:radiusA),normal,color,material});}
    for(int i=0;i<sides;++i){uint32_t lo=start+uint32_t(i),next=start+uint32_t((i+1)%sides),hi=lo+uint32_t(sides),hiNext=next+uint32_t(sides);mesh.indices.insert(mesh.indices.end(),{lo,hi,hiNext,lo,hiNext,next});
        float angle=2*Pi*float(i)/float(sides),nextAngle=2*Pi*float((i+1)%sides)/float(sides);Vec3 radial=side*std::cos(angle)+fore*std::sin(angle),nextRadial=side*std::cos(nextAngle)+fore*std::sin(nextAngle);
        triangle(mesh,a,a+radial*radiusA,a+nextRadial*radiusA,color,material);triangle(mesh,b,b+nextRadial*radiusB,b+radial*radiusB,color,material);
    }
}
struct Profile {float height,width,depth;};
void profileBody(Mesh& mesh,Vec3 position,float yaw,const Profile* profiles,int count,int segments,Vec3 color){
    const uint32_t start=uint32_t(mesh.vertices.size());
    for(int j=0;j<count;++j){
        const Profile& p=profiles[j];const Profile& low=profiles[std::max(j-1,0)];const Profile& high=profiles[std::min(j+1,count-1)];
        float dy=std::max(.001f,high.height-low.height);
        for(int i=0;i<segments;++i){float angle=2*Pi*float(i)/float(segments),x=std::sin(angle),z=std::cos(angle);
            Vec3 normal=normalized(Vec3{x/p.width,((low.width-high.width)*x*x/p.width+(low.depth-high.depth)*z*z/p.depth)/dy,z/p.depth});
            mesh.vertices.push_back({position+rotate({x*p.width,p.height,z*p.depth},yaw),rotate(normal,yaw),color,0});
        }
    }
    for(int j=0;j<count-1;++j)for(int i=0;i<segments;++i){uint32_t a=start+uint32_t(j*segments+i),b=start+uint32_t(j*segments+(i+1)%segments),c=b+uint32_t(segments),d=a+uint32_t(segments);mesh.indices.insert(mesh.indices.end(),{a,b,c,a,c,d});}
    for(int i=0;i<segments;++i){float a=2*Pi*float(i)/float(segments),b=2*Pi*float((i+1)%segments)/float(segments);const Profile& lo=profiles[0];const Profile& hi=profiles[count-1];
        triangle(mesh,position+Vec3{0,lo.height,0},position+rotate({std::sin(b)*lo.width,lo.height,std::cos(b)*lo.depth},yaw),position+rotate({std::sin(a)*lo.width,lo.height,std::cos(a)*lo.depth},yaw),color);
        triangle(mesh,position+Vec3{0,hi.height,0},position+rotate({std::sin(a)*hi.width,hi.height,std::cos(a)*hi.depth},yaw),position+rotate({std::sin(b)*hi.width,hi.height,std::cos(b)*hi.depth},yaw),color);
    }
}
void personMesh(Mesh& mesh,Vec3 position,float yaw,float phase,float motion,Vec3 shirt,Vec3 skin,bool armed,bool dead,uint32_t seed,bool closeDetail,bool riding=false){
    const int sides=closeDetail?10:6,headSegments=closeDetail?12:7,headRings=closeDetail?7:4;
    const Vec3 trousers{.055f,.065f,.081f},boots{.029f,.024f,.022f};
    const float stride=std::sin(phase)*.32f*motion,bob=dead?0.0f:std::abs(std::sin(phase))*.026f*motion;
    auto point=[&](Vec3 local){
        if(riding){if(local.y>.82f){local.z+=(local.y-.82f)*.25f;local.y-=.09f;}else local.y+=.10f;}
        if(dead)local={local.x,.16f-local.z,local.y-.84f};else local.y+=bob;
        return position+rotate(local,yaw);
    };
    const float bodyY=riding?-.09f:0;
    const Vec3 torsoPosition=position+Vec3{0,bob+bodyY,0};
    if(dead){ellipsoid(mesh,point({0,1.09f,0}),{.23f,.14f,.32f},yaw,shirt,sides,4);}
    else {
        const Profile torso[]={{.82f,.17f,.105f},{.90f,.195f,.126f},{1.10f,.215f,.137f},{1.27f,.235f,.130f},{1.37f,.174f,.094f}};
        profileBody(mesh,torsoPosition,yaw,torso,5,sides,shirt);
        ellipsoid(mesh,point({0,.81f,0}),{.184f,.11f,.118f},yaw,trousers,sides,4);
        if(closeDetail){
            const Vec3 inner=shirt*.50f;triangle(mesh,point({-.085f,1.35f,.090f}),point({0,1.04f,.141f}),point({.085f,1.35f,.090f}),inner);
            triangle(mesh,point({-.085f,1.36f,.104f}),point({-.023f,1.23f,.154f}),point({-.14f,1.30f,.12f}),shirt*1.2f);
            triangle(mesh,point({.085f,1.36f,.104f}),point({.14f,1.30f,.12f}),point({.023f,1.23f,.154f}),shirt*1.2f);
            tube(mesh,point({0,.87f,.131f}),point({0,1.05f,.143f}),.006f,.006f,{.42f,.47f,.43f},5);
            for(float side:{-1.0f,1.0f})tube(mesh,point({side*.11f,.94f,.128f}),point({side*.16f,1.01f,.126f}),.005f,.005f,shirt*.7f,5);
        }
    }
    tube(mesh,point({0,1.35f,0}),point({0,1.51f,0}),.068f,.064f,skin,sides);
    ellipsoid(mesh,point({0,1.63f,.006f}),{.134f,.178f,.116f},yaw,skin,headSegments,headRings);
    if(closeDetail){
        ellipsoid(mesh,point({0,1.535f,.026f}),{.093f,.074f,.094f},yaw,skin,10,4);
        for(float side:{-1.0f,1.0f}){
            ellipsoid(mesh,point({side*.131f,1.635f,-.002f}),{.025f,.043f,.025f},yaw,skin*.94f,6,4);
            ellipsoid(mesh,point({side*.052f,1.678f,.108f}),{.029f,.012f,.008f},yaw,{.81f,.79f,.71f},8,4);
            ellipsoid(mesh,point({side*.052f,1.678f,.116f}),{.010f,.009f,.003f},yaw,{.075f,.092f,.08f},6,3);
            tube(mesh,point({side*.030f,1.705f,.108f}),point({side*.079f,1.699f,.100f}),.007f,.005f,{.06f,.032f,.018f},5);
        }
        triangle(mesh,point({-.022f,1.648f,.109f}),point({0,1.616f,.151f}),point({.022f,1.648f,.109f}),skin*.91f);
        triangle(mesh,point({0,1.681f,.114f}),point({0,1.616f,.151f}),point({-.022f,1.648f,.109f}),skin*1.04f);
        triangle(mesh,point({0,1.681f,.114f}),point({.022f,1.648f,.109f}),point({0,1.616f,.151f}),skin*.96f);
        tube(mesh,point({-.034f,1.578f,.112f}),point({.034f,1.578f,.112f}),.006f,.006f,skin*.65f,5);
    }
    const float hairShade=.6f+random01(seed+91)*.6f;const Vec3 hair=Vec3{.064f,.035f,.020f}*hairShade;
    ellipsoid(mesh,point({0,1.756f,-.009f}),{.139f,.073f,.118f},yaw,hair,headSegments,closeDetail?5:3);
    if(closeDetail){
        ellipsoid(mesh,point({-.092f,1.729f,-.054f}),{.054f,.058f,.070f},yaw,hair*.88f,8,4);
        ellipsoid(mesh,point({.067f,1.772f,.018f}),{.061f,.048f,.091f},yaw,hair*1.12f,8,4);
    }
    for(float side:{-1.0f,1.0f}){
        float footLift=std::max(0.0f,-std::cos(phase)*side)*.13f*motion;
        Vec3 hip{side*.105f,.84f,0},knee{side*.113f,.47f+footLift*.4f,stride*side*.36f+.03f},ankle{side*.116f,.115f+footLift,stride*side};
        if(riding){hip={side*.13f,.83f,-.15f};knee={side*.26f,.55f,.30f};ankle={side*.24f,.20f,.12f};}
        tube(mesh,point(hip),point(knee),.105f,.083f,trousers,sides);tube(mesh,point(knee),point(ankle),.080f,.060f,trousers,sides);
        if(closeDetail)ellipsoid(mesh,point(knee),{.084f,.085f,.084f},yaw,trousers,8,4);
        ellipsoid(mesh,point(ankle+Vec3{0,-.054f,.058f}),{.085f,.064f,.164f},yaw,boots,sides,4);
        if(closeDetail){ellipsoid(mesh,point(ankle+Vec3{0,-.084f,.063f}),{.088f,.022f,.166f},yaw,{.016f,.018f,.019f},10,3);tube(mesh,point(ankle+Vec3{-.04f,-.015f,.087f}),point(ankle+Vec3{.04f,-.015f,.087f}),.006f,.006f,{.20f,.20f,.18f},5);}
        Vec3 shoulder{side*.225f,1.285f,0},elbow{side*.292f,1.02f,-stride*side*.55f},hand{side*.288f,.795f,-stride*side*.85f};
        if(armed){elbow={side*.255f,1.105f,.24f};hand={.13f,1.23f,.54f};}
        if(riding){shoulder={side*.225f,1.25f,.10f};elbow={side*.32f,1.12f,.35f};hand={side*.37f,1.07f,.57f};}
        ellipsoid(mesh,point(shoulder),{.100f,.118f,.111f},yaw,shirt,sides,4);
        const bool sleeves=(seed&1u)==0;const Vec3 forearmColor=sleeves?shirt*.91f:skin;
        tube(mesh,point(shoulder),point(elbow),.094f,.073f,shirt,sides);
        tube(mesh,point(elbow),point(hand),.071f,.047f,forearmColor,sides);
        ellipsoid(mesh,point(hand),{.052f,.073f,.037f},yaw,skin,sides,4);
        if(closeDetail&&sleeves)tube(mesh,point(lerp(elbow,hand,.85f)),point(lerp(elbow,hand,.97f)),.056f,.054f,shirt*.65f,sides);
    }
    if(armed){addBox(mesh,point({.13f,1.275f,.70f}),{.039f,.043f,.15f},{.049f,.053f,.061f},yaw,1);addBox(mesh,point({.13f,1.207f,.60f}),{.032f,.070f,.040f},{.039f,.032f,.025f},yaw,0);}
}
// Vehicle rendering helpers for the anonymous namespace in visuals.cpp.
// Local vehicle +Z points forwards. All surfaces retain outward winding.
void vehicleQuad(Mesh& mesh,Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 outward,Vec3 color,float material=0){
    if(dot(cross(b-a,c-a),outward)<0)addQuad(mesh,a,d,c,b,color,material);
    else addQuad(mesh,a,b,c,d,color,material);
}
void vehicleTriangle(Mesh& mesh,Vec3 a,Vec3 b,Vec3 c,Vec3 outward,Vec3 color,float material){
    if(dot(cross(b-a,c-a),outward)<0){Vec3 t=b;b=c;c=t;}
    const uint32_t first=uint32_t(mesh.vertices.size());
    const Vec3 n=normalized(cross(b-a,c-a));
    mesh.vertices.push_back({a,n,color,material});mesh.vertices.push_back({b,n,color,material});mesh.vertices.push_back({c,n,color,material});
    mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2});
}
void vehicleSmoothQuad(Mesh& mesh,const Vec3* p,const Vec3* n,Vec3 color,float material){
    uint32_t first=uint32_t(mesh.vertices.size());
    for(int i=0;i<4;++i)mesh.vertices.push_back({p[i],n[i],color,material});
    if(dot(cross(p[1]-p[0],p[2]-p[0]),n[0]+n[1]+n[2]+n[3])>=0)
        mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    else mesh.indices.insert(mesh.indices.end(),{first,first+3,first+2,first,first+2,first+1});
}
void vehicleWheel(Mesh& mesh,Vec3 center,float yaw,float radius,float halfWidth,float phase,bool detail){
    const int sides=detail?16:8;
    const Vec3 axis=right(yaw),fore=forward(yaw),rubber{.024f,.027f,.031f},alloy{.39f,.43f,.47f};
    const float x[4]={-halfWidth,-halfWidth*.70f,halfWidth*.70f,halfWidth};
    const float r[4]={radius*.79f,radius,radius,radius*.79f};
    const float nx[4]={-.86f,-.24f,.24f,.86f};
    auto radial=[&](float a){return Vec3{0,std::cos(a),0}+fore*std::sin(a);};
    for(int i=0;i<sides;++i){
        const float a=2*Pi*float(i)/float(sides),b=2*Pi*float(i+1)/float(sides);
        const Vec3 ra=radial(a),rb=radial(b);
        for(int j=0;j<3;++j){
            Vec3 p[4]={center+axis*x[j]+ra*r[j],center+axis*x[j]+rb*r[j],center+axis*x[j+1]+rb*r[j+1],center+axis*x[j+1]+ra*r[j+1]};
            Vec3 n[4]={normalized(ra+axis*nx[j]),normalized(rb+axis*nx[j]),normalized(rb+axis*nx[j+1]),normalized(ra+axis*nx[j+1])};
            vehicleSmoothQuad(mesh,p,n,rubber,0);
        }
        for(float side:{-1.0f,1.0f}){
            const Vec3 hub=center+axis*(halfWidth+.001f)*side,outward=axis*side;
            const float rim=radius*(detail?.57f:.79f);
            if(detail)vehicleQuad(mesh,hub+ra*radius*.79f,hub+rb*radius*.79f,hub+rb*rim,hub+ra*rim,outward,rubber,0);
            vehicleTriangle(mesh,hub,hub+ra*rim,hub+rb*rim,outward,detail?Vec3{.065f,.075f,.085f}:alloy,1);
        }
    }
    if(detail)for(float side:{-1.0f,1.0f}){
        const Vec3 hub=center+axis*(halfWidth+.005f)*side;
        for(int spoke=0;spoke<5;++spoke){
            const float a=phase+2*Pi*float(spoke)/5;
            const Vec3 ra=radial(a),tangent=radial(a+Pi*.5f);
            vehicleQuad(mesh,hub+ra*radius*.12f-tangent*.028f,hub+ra*radius*.55f-tangent*.045f,hub+ra*radius*.55f+tangent*.045f,hub+ra*radius*.12f+tangent*.028f,axis*side,alloy,1);
        }
    }
}
Vec3 craftRotation(Vec3 point,const Vehicle& vehicle){
    const float sr=std::sin(vehicle.roll),cr=std::cos(vehicle.roll),sp=std::sin(vehicle.pitch),cp=std::cos(vehicle.pitch);
    Vec3 rolled{point.x*cr+point.y*sr,-point.x*sr+point.y*cr,point.z};
    return rotate({rolled.x,rolled.y*cp+rolled.z*sp,-rolled.y*sp+rolled.z*cp},vehicle.yaw);
}
void craftMesh(Mesh& mesh,const Vehicle& vehicle,float time,bool detail){
    const size_t first=mesh.vertices.size();const int segments=detail?12:8;
    const Vec3 trim{.055f,.070f,.081f},chrome{.62f,.66f,.69f},cream{.83f,.82f,.72f};
    const Vec3 paint=vehicle.color*(.55f+.45f*clamp(vehicle.health/100,0,1));
    auto panel=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 normal,Vec3 color,float material=0){vehicleQuad(mesh,a,b,c,d,normal,color,material);};
    auto pipe=[&](Vec3 a,Vec3 b,float radius,Vec3 color,float material=1){tube(mesh,a,b,radius,radius,color,detail?8:5,material);};
    if(vehicle.kind==VehicleKind::Boat){
        const float z[5]={-2.65f,-1.65f,.50f,1.80f,2.85f},width[5]={1.0f,1.10f,1.03f,.68f,.055f};
        Vec3 section[5][6];
        for(int i=0;i<5;++i){const float bow=float(i)/4,top=.58f+.18f*bow*bow;
            section[i][0]={-width[i],top,z[i]};section[i][1]={-width[i]*.82f,-.18f+.17f*bow*bow,z[i]};section[i][2]={-width[i]*.10f,-.45f+.25f*bow*bow,z[i]};
            section[i][3]={width[i]*.10f,-.45f+.25f*bow*bow,z[i]};section[i][4]={width[i]*.82f,-.18f+.17f*bow*bow,z[i]};section[i][5]={width[i],top,z[i]};
        }
        for(int j=0;j<4;++j){
            for(int k=0;k<5;++k){Vec3 normal=normalized(Vec3{section[j][k].x+section[j][k+1].x,section[j][k].y+section[j][k+1].y-.6f,0});panel(section[j][k],section[j+1][k],section[j+1][k+1],section[j][k+1],normal,k==2?trim:paint,1);}
            panel(section[j][0],section[j][5],section[j+1][5],section[j+1][0],{0,1,0},cream);
            for(float side:{-1.0f,1.0f}){pipe({side*width[j],section[j][0].y+.035f,z[j]},{side*width[j+1],section[j+1][0].y+.035f,z[j+1]},.028f,chrome);}
        }
        for(int end:{0,4})for(int k=1;k<5;++k)vehicleTriangle(mesh,section[end][0],section[end][k],section[end][k+1],{0,0,end?1.0f:-1.0f},paint,1);
        // Raised foredeck, central helm, and upholstered seating leave a readable open-boat silhouette.
        panel({-.76f,.76f,1.25f},{.76f,.76f,1.25f},{.055f,.77f,2.80f},{-.055f,.77f,2.80f},{0,1,0},cream);
        addBox(mesh,{0,.91f,-.02f},{.34f,.33f,.36f},{.78f,.79f,.73f});
        panel({-.35f,1.18f,.29f},{.35f,1.18f,.29f},{.31f,1.60f,.12f},{-.31f,1.60f,.12f},{0,.2f,1},{.055f,.19f,.23f},1);
        for(float side:{-1.0f,1.0f})pipe({side*.35f,1.18f,.29f},{side*.31f,1.60f,.12f},.018f,chrome);
        pipe({-.31f,1.60f,.12f},{.31f,1.60f,.12f},.018f,chrome);
        for(float x:{-.54f,.54f}){
            ellipsoid(mesh,{x,.70f,-1.30f},{.39f,.12f,.39f},0,{.53f,.34f,.18f},segments,4);
            ellipsoid(mesh,{x,.92f,-1.65f},{.39f,.25f,.09f},0,{.58f,.39f,.22f},segments,4);
        }
        ellipsoid(mesh,{0,.28f,-2.80f},{.31f,.45f,.25f},0,trim,segments,5,1);
        pipe({0,.17f,-2.79f},{0,-.65f,-2.81f},.065f,chrome);
        if(detail){
            for(int i=0;i<12;++i){const float a=float(i)*2*Pi/12,b=float(i+1)*2*Pi/12;pipe({std::sin(a)*.15f,1.18f+std::cos(a)*.13f,-.415f},{std::sin(b)*.15f,1.18f+std::cos(b)*.13f,-.415f},.017f,trim,0);}
            addBox(mesh,{0,1.18f,-.385f},{.11f,.065f,.018f},{.025f,.045f,.052f});
            pipe({-.75f,.72f,-2.30f},{-.75f,1.28f,-2.30f},.018f,chrome);
            ellipsoid(mesh,{-.75f,1.29f,-2.30f},{.04f,.045f,.04f},0,{1,.93f,.77f},6,3,2);
        }
        ellipsoid(mesh,{0,.83f,1.6f},{.08f,.055f,.06f},0,{1,.93f,.80f},8,4,2);
        ellipsoid(mesh,{-.95f,.68f,1.0f},{.045f,.04f,.085f},0,{1,.03f,.015f},6,3,2);
        ellipsoid(mesh,{.95f,.68f,1.0f},{.045f,.04f,.085f},0,{.03f,1,.17f},6,3,2);
    }else{
        ellipsoid(mesh,{0,1.28f,-.10f},{.47f,.48f,3.34f},0,paint,segments,detail?10:6,1);
        ellipsoid(mesh,{0,1.27f,2.62f},{.38f,.36f,.67f},0,paint,segments,5,1);
        ellipsoid(mesh,{0,1.64f,.80f},{.405f,.30f,.91f},0,{.045f,.12f,.16f},segments,6,1);
        for(float side:{-1.0f,1.0f}){
            // Tapered airfoil sections, each with a shallow upper crown and dihedral toward the tip.
            const float span[4]={.35f,2.2f,4.70f,5.15f},leading[4]={.94f,.73f,.22f,.0f},trailing[4]={-1.12f,-1.08f,-.94f,-.86f};
            Vec3 wing[4][4];
            for(int j=0;j<4;++j){float x=side*span[j],y=1.19f+span[j]*.025f;wing[j][0]={x,y,leading[j]};wing[j][1]={x,y+.105f,lerp(leading[j],trailing[j],.32f)};wing[j][2]={x,y+.018f,trailing[j]};wing[j][3]={x,y-.047f,lerp(leading[j],trailing[j],.40f)};}
            for(int j=0;j<3;++j)for(int k=0;k<4;++k){int next=(k+1)%4;panel(wing[j][k],wing[j+1][k],wing[j+1][next],wing[j][next],{0,k<2?1.0f:-1.0f,0},paint,1);}
            panel(wing[3][0],wing[3][1],wing[3][2],wing[3][3],{side,0,0},cream,1);
            panel({side*2.3f,1.282f,-.83f},{side*4.70f,1.345f,-.72f},{side*4.70f,1.326f,-.925f},{side*2.3f,1.27f,-1.055f},{0,1,0},cream,1);
            panel({side*.10f,1.45f,-2.20f},{side*1.90f,1.51f,-2.80f},{side*1.85f,1.49f,-3.30f},{side*.10f,1.43f,-3.18f},{0,1,0},paint,1);
            panel({side*.10f,1.41f,-3.18f},{side*1.85f,1.45f,-3.30f},{side*1.90f,1.47f,-2.80f},{side*.10f,1.41f,-2.20f},{0,-1,0},paint,1);
            pipe({side*.31f,.95f,-.20f},{side*1.07f,.27f,-.25f},.043f,chrome);
            vehicleWheel(mesh,{side*1.08f,.245f,-.25f},0,.245f,.095f,time*vehicle.speed/.245f,detail);
            ellipsoid(mesh,{side*5.15f,1.335f,-.38f},{.075f,.038f,.09f},0,side<0?Vec3{1,.025f,.012f}:Vec3{.03f,1,.14f},6,3,2);
            if(detail){pipe({side*.25f,1.83f,.10f},{side*.28f,1.75f,1.51f},.021f,cream);pipe({side*.28f,1.75f,1.51f},{side*.35f,1.45f,1.68f},.021f,cream);}
        }
        for(float side:{-1.0f,1.0f}){
            panel({side*.055f,1.36f,-3.32f},{side*.055f,2.66f,-3.04f},{side*.055f,2.30f,-2.48f},{side*.055f,1.36f,-2.17f},{side,0,0},paint,1);
            panel({side*.058f,1.48f,-3.28f},{side*.058f,2.55f,-3.04f},{side*.058f,2.40f,-2.86f},{side*.058f,1.48f,-3.0f},{side,0,0},cream,1);
        }
        pipe({0,.99f,2.46f},{0,.19f,2.46f},.038f,chrome);
        vehicleWheel(mesh,{0,.19f,2.46f},0,.19f,.070f,time*vehicle.speed/.19f,detail);
        ellipsoid(mesh,{0,1.27f,3.31f},{.15f,.15f,.23f},0,chrome,segments,4,1);
        const float rotation=time*(vehicle.throttle*85+std::min(vehicle.speed,15.0f)*.9f);
        for(int i=0;i<2;++i){float angle=rotation+float(i)*Pi;Vec3 radial{std::cos(angle),std::sin(angle),0},edge{-std::sin(angle),std::cos(angle),0};Vec3 hub{0,1.27f,3.40f};
            panel(hub+radial*.17f-edge*.08f,hub+radial*1.02f-edge*.045f,hub+radial*1.02f+edge*.045f,hub+radial*.17f+edge*.08f,{0,0,1},trim,1);
        }
        ellipsoid(mesh,{0,1.10f,3.24f},{.065f,.042f,.030f},0,{1,.93f,.80f},8,4,2);
        ellipsoid(mesh,{0,2.69f,-3.03f},{.05f,.045f,.06f},0,{1,.93f,.83f},6,3,2);
    }
    for(size_t i=first;i<mesh.vertices.size();++i){Vertex& vertex=mesh.vertices[i];vertex.position=vehicle.position+craftRotation(vertex.position,vehicle);vertex.normal=normalized(craftRotation(vertex.normal,vehicle));}
}
void vehicleMesh(Mesh& mesh,const Vehicle& v,float time,uint32_t seed,bool closeDetail){
    if(v.kind==VehicleKind::Boat||v.kind==VehicleKind::Aircraft){craftMesh(mesh,v,time,closeDetail);return;}
    auto point=[&](Vec3 p){return v.position+rotate(p,v.yaw);};
    auto box=[&](Vec3 p,Vec3 half,Vec3 color,float material=0){addBox(mesh,point(p),half,color,v.yaw,material);};
    auto panel=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 normal,Vec3 color,float material=0){vehicleQuad(mesh,point(a),point(b),point(c),point(d),rotate(normal,v.yaw),color,material);};
    auto pipe=[&](Vec3 a,Vec3 b,float radius,Vec3 color,float material=1){tube(mesh,point(a),point(b),radius,radius,color,closeDetail?6:4,material);};
    const Vec3 trim{.035f,.042f,.05f},chrome{.52f,.57f,.60f},glass{.065f,.15f,.19f};
    const bool taxi=!v.police&&seed%7u==0u;
    const bool coupe=!v.police&&!taxi&&seed%3u==0u;
    const Vec3 paint=(taxi?Vec3{.94f,.57f,.025f}:v.color)*(.55f+.45f*clamp(v.health/100,0,1));
    if(v.kind==VehicleKind::Motorcycle){
        const int segments=closeDetail?12:8,rings=closeDetail?6:4;
        for(float z:{-.78f,.80f})vehicleWheel(mesh,point({0,.34f,z}),v.yaw+(z>0?v.steer*.35f:0),.34f,.105f,time*v.speed/.34f,closeDetail);
        ellipsoid(mesh,point({0,.83f,.19f}),{.24f,.21f,.39f},v.yaw,paint,segments,rings,1);
        ellipsoid(mesh,point({0,.85f,-.36f}),{.22f,.085f,.35f},v.yaw,trim,segments,4,0);
        ellipsoid(mesh,point({0,.48f,-.03f}),{.20f,.22f,.22f},v.yaw,chrome,segments,4,1);
        for(float side:{-1.0f,1.0f}){
            pipe({side*.14f,.35f,-.78f},{side*.16f,.55f,-.05f},.035f,trim);
            pipe({side*.16f,.55f,-.05f},{side*.13f,.99f,.50f},.034f,trim);
            pipe({side*.13f,.34f,.80f},{side*.13f,1.10f,.57f},.031f,chrome);
            pipe({side*.30f,.32f,-.65f},{side*.27f,.34f,.05f},.054f,chrome);
        }
        pipe({-.43f,1.10f,.53f},{.43f,1.10f,.53f},.026f,chrome);
        for(float side:{-1.0f,1.0f})pipe({side*.31f,1.10f,.53f},{side*.45f,1.10f,.53f},.036f,trim,0);
        // Curved painted fenders follow each tire's crown.
        for(float z:{-.78f,.80f})for(int k=0;k<6;++k){
            float a=-1.04f+2.08f*k/6,b=-1.04f+2.08f*(k+1)/6;
            panel({-.13f,.34f+.39f*std::cos(a),z+.39f*std::sin(a)},{.13f,.34f+.39f*std::cos(a),z+.39f*std::sin(a)},{.13f,.34f+.39f*std::cos(b),z+.39f*std::sin(b)},{-.13f,.34f+.39f*std::cos(b),z+.39f*std::sin(b)},{0,std::cos((a+b)*.5f),std::sin((a+b)*.5f)},paint,1);
        }
        box({0,1.00f,.69f},{.13f,.105f,.06f},{1,.92f,.72f},2);
        box({0,.81f,-.70f},{.105f,.045f,.024f},{.77f,.025f,.015f},2);
        if(closeDetail){
            for(float side:{-1.0f,1.0f}){
                pipe({side*.32f,1.10f,.53f},{side*.40f,1.33f,.52f},.014f,chrome);
                ellipsoid(mesh,point({side*.43f,1.33f,.51f}),{.10f,.055f,.035f},v.yaw,chrome,8,4,1);
                for(int k=0;k<4;++k)box({side*.205f,.41f+k*.045f,-.04f},{.015f,.012f,.14f},trim,1);
            }
        }
        return;
    }
    const float roofY=coupe?1.32f:1.47f,roofFront=coupe?.27f:.42f,roofRear=coupe?-.43f:-.68f;
    const float bottomFront=1.02f,bottomRear=coupe?-1.28f:-1.19f;
    // Eight-sided cross sections bevel both the belt line and lower sill.
    const float stations[6]={-2.08f,-1.87f,-1.10f,.96f,1.83f,2.08f};
    const float widths[6]={.75f,.91f,.92f,.92f,.89f,.76f};
    const float tops[6]={.72f,.87f,.94f,.94f,.84f,.70f};
    Vec3 hull[6][8];
    for(int s=0;s<6;++s){float w=widths[s],z=stations[s],top=tops[s];
        hull[s][0]={-w*.87f,.36f,z};hull[s][1]={-w,.47f,z};hull[s][2]={-w,top-.10f,z};hull[s][3]={-w*.87f,top,z};
        hull[s][4]={w*.87f,top,z};hull[s][5]={w,top-.10f,z};hull[s][6]={w,.47f,z};hull[s][7]={w*.87f,.36f,z};
    }
    for(int s=0;s<5;++s)for(int k=0;k<8;++k){
        const int n=(k+1)%8;Vec3 normal=(hull[s][k]+hull[s][n])*.5f-Vec3{0,.63f,stations[s]};
        panel(hull[s][k],hull[s+1][k],hull[s+1][n],hull[s][n],normal,paint,1);
    }
    for(int end:{0,5})for(int k=1;k<7;++k)vehicleTriangle(mesh,point(hull[end][0]),point(hull[end][k]),point(hull[end][k+1]),rotate({0,0,end==0?-1.0f:1.0f},v.yaw),paint,1);
    // Body-colour trapezoid surround, with glass inset from the pillars.
    panel({-.80f,.93f,bottomFront},{.80f,.93f,bottomFront},{.66f,roofY,roofFront},{-.66f,roofY,roofFront},{0,1,1},paint,1);
    panel({.80f,.93f,bottomRear},{-.80f,.93f,bottomRear},{-.66f,roofY,roofRear},{.66f,roofY,roofRear},{0,1,-1},paint,1);
    const float glassLowT=(.995f-.93f)/(roofY-.93f),glassHighT=(roofY-.045f-.93f)/(roofY-.93f);
    const float frontLow=lerp(bottomFront,roofFront,glassLowT)+.007f,frontHigh=lerp(bottomFront,roofFront,glassHighT)+.007f;
    const float rearLow=lerp(bottomRear,roofRear,glassLowT)-.007f,rearHigh=lerp(bottomRear,roofRear,glassHighT)-.007f;
    panel({-.72f,.995f,frontLow},{.72f,.995f,frontLow},{.60f,roofY-.045f,frontHigh},{-.60f,roofY-.045f,frontHigh},{0,1,1},glass,1);
    panel({.72f,.995f,rearLow},{-.72f,.995f,rearLow},{-.60f,roofY-.045f,rearHigh},{.60f,roofY-.045f,rearHigh},{0,1,-1},glass*.80f,1);
    for(float side:{-1.0f,1.0f}){
        panel({side*.80f,.93f,bottomRear},{side*.80f,.93f,bottomFront},{side*.66f,roofY,roofFront},{side*.66f,roofY,roofRear},{side,0,0},paint,1);
        const float lowerX=side*(lerp(.80f,.66f,(.987f-.93f)/(roofY-.93f))+.005f);
        const float upperX=side*(lerp(.80f,.66f,(roofY-.055f-.93f)/(roofY-.93f))+.005f);
        panel({lowerX,.987f,bottomRear+.15f},{lowerX,.987f,bottomFront-.15f},{upperX,roofY-.055f,roofFront-.055f},{upperX,roofY-.055f,roofRear+.065f},{side,0,0},glass*.86f,1);
        if(closeDetail){
            // B pillar sits outside the glass plane and splits the doors.
            const float pillarLow=side*(lerp(.80f,.66f,(.983f-.93f)/(roofY-.93f))+.009f);
            const float pillarHigh=side*(lerp(.80f,.66f,(roofY-.048f-.93f)/(roofY-.93f))+.009f);
            panel({pillarLow,.983f,-.14f},{pillarLow,.983f,-.045f},{pillarHigh,roofY-.048f,-.045f},{pillarHigh,roofY-.048f,-.14f},{side,0,0},trim,0);
            box({side*.915f,.49f,0},{.025f,.055f,1.03f},trim);
            box({side*.923f,.80f,coupe?-.33f:.42f},{.012f,.023f,.095f},chrome,1);
            if(!coupe)box({side*.923f,.80f,-.67f},{.012f,.023f,.095f},chrome,1);
            box({side*.985f,1.015f,.69f},{.12f,.062f,.10f},paint,1);
            box({side*.989f,1.017f,.587f},{.098f,.046f,.004f},chrome,1);
        }
        for(float z:{-1.31f,1.30f})vehicleWheel(mesh,point({side*.92f,.385f,z}),v.yaw+(z>0?v.steer*.40f:0),.385f,.13f,time*v.speed/.385f,closeDetail);
        // Lamps conform to the narrow nose and tail sections.
        panel({side*.35f,.57f,2.084f},{side*.72f,.57f,2.084f},{side*.72f,.70f,2.084f},{side*.35f,.70f,2.084f},{0,0,1},{1,.92f,.75f},2);
        panel({side*.36f,.58f,-2.084f},{side*.72f,.58f,-2.084f},{side*.72f,.70f,-2.084f},{side*.36f,.70f,-2.084f},{0,0,-1},{.73f,.025f,.014f},2);
        if(v.police)panel({side*.922f,.51f,-.72f},{side*.922f,.51f,.63f},{side*.922f,.80f,.63f},{side*.922f,.80f,-.72f},{side,0,0},{.82f,.85f,.86f},0);
    }
    box({0,roofY+.018f,(roofFront+roofRear)*.5f},{.668f,.031f,(roofFront-roofRear)*.5f+.015f},paint,1);
    panel({-.27f,.51f,2.087f},{.27f,.51f,2.087f},{.27f,.67f,2.087f},{-.27f,.67f,2.087f},{0,0,1},trim,0);
    if(closeDetail){
        for(int row=0;row<3;++row)box({0,.535f+row*.045f,2.092f},{.24f,.006f,.004f},chrome,1);
        box({0,.44f,2.073f},{.69f,.035f,.038f},trim);
        box({0,.44f,-2.073f},{.69f,.035f,.038f},trim);
        box({0,.58f,-2.095f},{.18f,.062f,.009f},{.79f,.82f,.74f});
        for(float side:{-1.0f,1.0f})pipe({side*.57f,.34f,-1.94f},{side*.57f,.34f,-2.15f},.043f,chrome);
        // Small hood creases catch the moving sun without a texture asset.
        for(float side:{-1.0f,1.0f})panel({side*.38f,.937f,1.00f},{side*.39f,.941f,1.00f},{side*.52f,.849f,1.80f},{side*.49f,.849f,1.80f},{0,1,0},paint*.83f,1);
    }
    if(taxi){box({0,roofY+.115f,-.10f},{.31f,.075f,.12f},{1,.79f,.18f},2);box({0,roofY+.115f,.024f},{.20f,.037f,.004f},trim);}
    if(v.police){
        box({0,roofY+.105f,-.18f},{.62f,.041f,.115f},trim);
        bool flash=std::sin(time*17)>0;
        box({-.35f,roofY+.19f,-.18f},{.23f,.058f,.095f},flash?Vec3{.06f,.22f,1}:Vec3{.025f,.045f,.12f},flash?2.0f:0.0f);
        box({.35f,roofY+.19f,-.18f},{.23f,.058f,.095f},flash?Vec3{.14f,.02f,.02f}:Vec3{1,.035f,.02f},flash?0.0f:2.0f);
    }
}
void medicalCase(Mesh& mesh,Vec3 base,float yaw){
    const Vec3 shell{.87f,.86f,.77f},red{.76f,.075f,.045f},trim{.095f,.11f,.12f};
    auto point=[&](Vec3 p){return base+rotate(p,yaw);};
    addBox(mesh,point({0,.17f,0}),{.28f,.17f,.19f},shell,yaw);
    addBox(mesh,point({0,.27f,0}),{.287f,.018f,.197f},trim,yaw);
    for(float side:{-1.0f,1.0f}){
        addBox(mesh,point({side*.18f,.26f,.20f}),{.026f,.058f,.015f},trim,yaw);
        tube(mesh,point({side*.09f,.345f,0}),point({side*.09f,.405f,0}),.018f,.018f,trim,6);
    }
    tube(mesh,point({-.09f,.405f,0}),point({.09f,.405f,0}),.018f,.018f,trim,6);
    addBox(mesh,point({0,.15f,.195f}),{.075f,.022f,.009f},red,yaw);
    addBox(mesh,point({0,.15f,.195f}),{.023f,.072f,.010f},red,yaw);
    addBox(mesh,point({0,.345f,0}),{.09f,.007f,.028f},red,yaw);
    addBox(mesh,point({0,.345f,0}),{.028f,.008f,.09f},red,yaw);
}
}

Mesh Game::dynamicMesh() const {
    Mesh mesh;mesh.vertices.reserve(125000);mesh.indices.reserve(230000);
    for(size_t i=0;i<vehicles.size();++i){
        const Vehicle& v=vehicles[i];const float distance=planarDistance(v.position,player);if(distance>310)continue;
        vehicleMesh(mesh,v,time,i==0?1u:hash32(uint32_t(i)+319u),distance<48);
    }
    for(size_t i=0;i<pedestrians.size();++i){
        const Pedestrian& p=pedestrians[i];const float distance=planarDistance(p.position,player);if(distance>180)continue;
        Vec3 shirt=i<4?Vec3{.035f,.065f,.12f}:Vec3{.13f+random01(uint32_t(i)*13)*.55f,.09f+random01(uint32_t(i)*29)*.55f,.10f+random01(uint32_t(i)*43)*.55f};
        Vec3 skin=Vec3{.72f,.47f,.31f}*(.65f+random01(uint32_t(i)*17)*.4f);
        personMesh(mesh,p.position,p.yaw,p.phase,p.panic>0?1.0f:.60f,shirt,skin,i<4&&wanted>0,p.health<=0,uint32_t(i),distance<32);
        if(i<4&&p.health>0){
            ellipsoid(mesh,p.position+rotate({0,1.795f,-.015f},p.yaw),{.143f,.047f,.124f},p.yaw,shirt,10,3);
            ellipsoid(mesh,p.position+rotate({0,1.777f,.102f},p.yaw),{.122f,.010f,.081f},p.yaw,shirt*.65f,10,3);
        }
    }
    if(occupied<0)personMesh(mesh,player,yaw,playerPhase,playerMotion,{.035f,.16f,.19f},{.64f,.40f,.27f},aiming||shotFlash>0,false,24,true);
    else if(size_t(occupied)<vehicles.size()&&vehicles[size_t(occupied)].kind==VehicleKind::Motorcycle)
        personMesh(mesh,player,vehicles[size_t(occupied)].yaw,0,0,{.035f,.16f,.19f},{.64f,.40f,.27f},false,false,24,true,true);
    if(occupied>=0&&size_t(occupied)<vehicles.size()&&vehicles[size_t(occupied)].kind==VehicleKind::Boat){
        const Vehicle& boat=vehicles[size_t(occupied)];const size_t first=mesh.vertices.size();
        personMesh(mesh,{0,.13f,-.85f},0,0,0,{.035f,.16f,.19f},{.64f,.40f,.27f},false,false,24,true,true);
        for(size_t i=first;i<mesh.vertices.size();++i){Vertex& vertex=mesh.vertices[i];vertex.position=boat.position+craftRotation(vertex.position,boat);vertex.normal=normalized(craftRotation(vertex.normal,boat));}
        if(std::abs(boat.speed)>1){const float trail=std::min(12.0f,std::abs(boat.speed)*.65f);const Vec3 aft=boat.position-forward(boat.yaw)*2.65f;
            for(float side:{-1.0f,1.0f}){Vec3 a=aft+right(boat.yaw)*(side*.7f),b=aft-forward(boat.yaw)*trail+right(boat.yaw)*(side*(1.3f+trail*.12f));Vec3 c=b+right(boat.yaw)*(side*.2f);a.y=b.y=c.y=World::WaterLevel+.035f;vehicleTriangle(mesh,a,b,c,{0,1,0},{.59f,.77f,.76f},0);}
        }
    }
    if(activeMission==4){
        const Vec3 clinic{3090,World::WaterLevel,1080};
        const Vec3 coat{.79f,.27f,.065f},skin{.59f,.36f,.23f};
        if(missionStage<2&&planarDistance(player,clinic)<230){
            const Vec3 survivor=clinic+Vec3{0,.58f,-1.20f};
            const float facing=std::atan2(player.x-survivor.x,player.z-survivor.z);
            personMesh(mesh,survivor,facing,time*.8f,.04f,coat,skin,false,false,47,planarDistance(player,clinic)<45);
            medicalCase(mesh,clinic+Vec3{-.70f,.58f,-.15f},-.20f);
            if(missionHold>0){
                const float progress=clamp(missionHold/3,0,1);
                for(int i=0;i<32&&float(i)/32<progress;++i){const float a=float(i)*2*Pi/32,b=float(i+1)*2*Pi/32;
                    tube(mesh,survivor+Vec3{std::sin(a)*.6f,2.15f,std::cos(a)*.6f},survivor+Vec3{std::sin(b)*.6f,2.15f,std::cos(b)*.6f},.025f,.025f,{.12f,1,.58f},4,2);}
            }
        }else if(missionStage>=2){
            if(occupied>=0&&size_t(occupied)<vehicles.size()&&vehicles[size_t(occupied)].kind==VehicleKind::Boat){
                const Vehicle& boat=vehicles[size_t(occupied)];const size_t first=mesh.vertices.size();
                personMesh(mesh,{-.54f,.04f,-1.31f},0,0,0,coat,skin,false,false,47,true,true);
                medicalCase(mesh,{.56f,.64f,.67f},0);
                for(size_t i=first;i<mesh.vertices.size();++i){Vertex& vertex=mesh.vertices[i];vertex.position=boat.position+craftRotation(vertex.position,boat);vertex.normal=normalized(craftRotation(vertex.normal,boat));}
            }else if(occupied<0){
                Vec3 companion=world.move(player,right(yaw)*.95f,.30f);companion.y=player.y;
                personMesh(mesh,companion,yaw,playerPhase+.4f,playerMotion,coat,skin,false,false,47,true);
                medicalCase(mesh,companion+rotate({-.42f,.57f,0},yaw),yaw);
            }
        }
    }
    if(shotFlash>0&&occupied<0){ellipsoid(mesh,shotOrigin,{.055f,.055f,.12f},yaw,{1,.73f,.22f},6,3,2);tube(mesh,shotOrigin,shotEnd,.012f,.007f,{1,.68f,.23f},4,2);}
    if(missionInfo()){
        Vec3 target=missionTarget();
        if(activeMission==5&&missionStage<3&&planarDistance(target,player)<1400){
            const Vec3 previous=missionStage==0?Vec3{-3200,0,-1190}:(missionStage==1?Vec3{-3200,0,-500}:Vec3{-2600,0,0});
            Vec3 approach=target-previous;approach.y=0;approach=normalized(approach);
            const Vec3 crossbar{approach.z,0,-approach.x};const float halfHeight=missionStage==0?40.0f:50.0f;
            const Vec3 gateColor{.12f,.87f,.71f};
            for(int i=0;i<48;++i){const float a=float(i)*2*Pi/48,b=float(i+1)*2*Pi/48;
                tube(mesh,target+crossbar*(std::sin(a)*70)+Vec3{0,std::cos(a)*halfHeight,0},target+crossbar*(std::sin(b)*70)+Vec3{0,std::cos(b)*halfHeight,0},.42f,.42f,gateColor,5,2);}
            for(int i=0;i<3;++i){const Vec3 pip=target+Vec3{0,halfHeight+4,0}+crossbar*(float(i-1)*3.2f);
                ellipsoid(mesh,pip,{.82f,.82f,.82f},0,i<=missionStage?Vec3{1,.65f,.13f}:Vec3{.18f,.25f,.28f},6,3,i<=missionStage?2.0f:0.0f);}
        }else{
            target.y=std::max(world.height(target.x,target.z),World::WaterLevel);
            if(activeMission==5&&missionStage>=3&&planarDistance(target,player)<900){
                const Vec3 color{.10f,.88f,.63f};
                for(float x:{-3214.0f,-3186.0f})tube(mesh,{x,4.08f,-1220},{x,4.08f,-780},.10f,.10f,color,4,2);
                for(float z:{-1220.0f,-780.0f})tube(mesh,{-3214,4.08f,z},{-3186,4.08f,z},.10f,.10f,color,4,2);
            }
            if(planarDistance(target,player)<420){
            const Vec3 color=activeMission<0?Vec3{1,.58f,.08f}:Vec3{.1f,.88f,.68f};const float radius=activeMission<0?2.5f:(activeMission==4&&missionStage<2?14.0f:3.8f);
            for(int i=0;i<32;++i){float a=float(i)*2*Pi/32,b=float(i+1)*2*Pi/32;Vec3 p=target+Vec3{std::sin(a)*radius,.08f,std::cos(a)*radius},q=target+Vec3{std::sin(b)*radius,.08f,std::cos(b)*radius};tube(mesh,p,q,.045f,.045f,color,4,2);}
            const float bob=std::sin(time*2)*.15f;ellipsoid(mesh,target+Vec3{0,3.2f+bob,0},{.25f,.42f,.25f},time*.6f,color,6,3,2);
            }
        }
    }
    for(Vec3 marker:{Garage,Outfitter}){if(planarDistance(marker,player)>160)continue;marker.y=world.height(marker.x,marker.z);ellipsoid(mesh,marker+Vec3{0,2.4f,0},{.20f,.28f,.20f},time*.35f,{.13f,.46f,1},6,3,2);}
    return mesh;
}

std::vector<Light> Game::lightSources() const {
    struct Candidate {Light light;float priority;size_t order;};
    std::vector<Candidate> candidates;
    candidates.reserve(world.chunks.size()*12+vehicles.size()*2+1);
    const float sunAngle=(dayTime-6.0f)*Pi/12.0f;
    const float sunHeight=std::sin(sunAngle)/std::sqrt(1.0f+.27f*.27f);
    float daylight=clamp((sunHeight+.10f)/.30f,0,1);
    daylight=daylight*daylight*(3-2*daylight);
    const float night=1-daylight;
    const Vec3 observer=player+Vec3{0,1.2f,0};
    auto collect=[&](Light light){
        if(!std::isfinite(light.position.x)||!std::isfinite(light.position.y)||!std::isfinite(light.position.z)||
           !std::isfinite(light.radius)||!std::isfinite(light.intensity)||light.radius<=0||light.intensity<=.001f||
           !std::isfinite(light.color.x)||!std::isfinite(light.color.y)||!std::isfinite(light.color.z)||
           light.color.x<0||light.color.y<0||light.color.z<0||dot(light.color,light.color)<1e-8f||
           !std::isfinite(light.direction.x)||!std::isfinite(light.direction.y)||!std::isfinite(light.direction.z)||
           !std::isfinite(light.cone)||light.cone< -1||light.cone>=1)return;
        const float distance=length(light.position-observer);
        if(distance>light.radius+70)return;
        light.direction=length(light.direction)>.0001f?normalized(light.direction):Vec3{0,-1,0};
        const float outsideReach=std::max(0.0f,distance-light.radius);
        // Prefer nearby emitters whose illumination can actually reach the camera's neighbourhood.
        candidates.push_back({light,distance+outsideReach*4,candidates.size()});
    };
    if(night>.00001f)for(const Chunk& chunk:world.chunks)for(Light light:chunk.lights){light.intensity*=night;collect(light);}
    for(const Vehicle& vehicle:vehicles){
        if(vehicle.health<=0)continue;
        auto point=[&](Vec3 local){return vehicle.position+rotate(local,vehicle.yaw);};
        if(vehicle.kind==VehicleKind::Boat||vehicle.kind==VehicleKind::Aircraft){
            if(night>.00001f){
                const bool plane=vehicle.kind==VehicleKind::Aircraft;
                for(float side:{-1.0f,1.0f}){Light navigation;Vec3 local=plane?Vec3{side*5.15f,1.335f,-.38f}:Vec3{side*.95f,.68f,1.0f};navigation.position=vehicle.position+craftRotation(local,vehicle);navigation.color=side<0?Vec3{1,.025f,.012f}:Vec3{.03f,1,.14f};navigation.radius=6;navigation.intensity=5*night;navigation.cone=-1;collect(navigation);}
                Light beam;beam.position=vehicle.position+craftRotation(plane?Vec3{0,1.1f,3.24f}:Vec3{0,.83f,1.6f},vehicle);beam.direction=normalized(craftRotation({0,-.07f,1},vehicle));beam.color={1,.93f,.80f};beam.radius=plane?58.0f:34.0f;beam.intensity=(plane?250.0f:125.0f)*night;beam.cone=plane?.91f:.84f;collect(beam);
            }
            continue;
        }
        if(night>.00001f){
            Light headlight;headlight.color={1,.92f,.75f};headlight.cone=.85f;
            const bool bike=vehicle.kind==VehicleKind::Motorcycle;
            headlight.direction=normalized(forward(vehicle.yaw)+Vec3{0,bike?-.055f:-.035f,0});
            headlight.radius=bike?38.0f:45.0f;headlight.intensity=(bike?185.0f:220.0f)*night;
            if(bike){headlight.position=point({0,1.0f,.755f});collect(headlight);}
            else for(float side:{-1.0f,1.0f}){headlight.position=point({side*.535f,.635f,2.10f});collect(headlight);}
        }
        if(vehicle.police&&(night>.00001f||wanted>0)){
            const bool blue=std::sin(time*17)>0;
            Light flasher;flasher.position=point({blue?-.35f:.35f,1.66f,-.18f});
            flasher.color=blue?Vec3{.06f,.22f,1}:Vec3{1,.035f,.02f};flasher.direction={0,1,0};
            flasher.radius=18;flasher.intensity=wanted>0?90+60*daylight:90*night;flasher.cone=-1;collect(flasher);
        }
    }
    if(shotFlash>0&&occupied<0){
        Light muzzle;muzzle.position=shotOrigin;muzzle.radius=8;muzzle.color={1,.73f,.22f};
        muzzle.intensity=45*clamp(shotFlash/.07f,0,1);muzzle.direction={0,1,0};muzzle.cone=-1;collect(muzzle);
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
        return a.priority<b.priority||(a.priority==b.priority&&a.order<b.order);
    });
    std::vector<Light> result;result.reserve(std::min(size_t(64),candidates.size()));
    for(size_t i=0;i<candidates.size()&&i<64;++i)result.push_back(candidates[i].light);
    return result;
}

}
