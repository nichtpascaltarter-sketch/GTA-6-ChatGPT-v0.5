#include "game.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mc {
namespace {
constexpr float Lane=3.2f;
constexpr int Magazine=30;
constexpr Vec3 Garage{140,0,128};
constexpr Vec3 Outfitter{-116,0,128};
constexpr Vec3 DispatchDestination{-372,0,256};
constexpr Vec3 SignalDestination{780,0,-384};
float planarDistance(Vec3 a,Vec3 b) {a.y=b.y=0;return length(a-b);}
float roadGrid(Vec3 p) {
    if(p.x>=-1664&&p.x<=1536&&p.z>=-1792&&p.z<=2048)return 128;
    if(p.x>=-3072&&p.x<=2048&&p.z>=-3072&&p.z<=3072)return 256;
    return 512;
}
Vec3 atGround(const World& world,Vec3 p){p.y=world.height(p.x,p.z);return p;}
bool finite(float f){return std::isfinite(f);}
bool finite(Vec3 p){return finite(p.x)&&finite(p.y)&&finite(p.z);}
Vec3 rotate(Vec3 p,float yaw){return right(yaw)*p.x+Vec3{0,p.y,0}+forward(yaw)*p.z;}
float towards(float current,float target,float amount){return current<target?std::min(current+amount,target):std::max(current-amount,target);}
float rayBox(Vec3 origin,Vec3 direction,const Box& box,float maximum){
    float nearT=0,farT=maximum;
    const float o[3]={origin.x,origin.y,origin.z},d[3]={direction.x,direction.y,direction.z};
    const float lo[3]={box.min.x,box.min.y,box.min.z},hi[3]={box.max.x,box.max.y,box.max.z};
    for(int k=0;k<3;++k){
        if(std::abs(d[k])<1e-6f){if(o[k]<lo[k]||o[k]>hi[k])return maximum;}
        else {float a=(lo[k]-o[k])/d[k],b=(hi[k]-o[k])/d[k];if(a>b)std::swap(a,b);nearT=std::max(nearT,a);farT=std::min(farT,b);if(nearT>farT)return maximum;}
    }
    return nearT;
}
float raySphere(Vec3 origin,Vec3 direction,Vec3 center,float radius,float maximum){
    Vec3 oc=origin-center;float b=dot(oc,direction),c=dot(oc,oc)-radius*radius,disc=b*b-c;
    if(disc<0)return maximum;
    float t=-b-std::sqrt(disc);if(t<0)t=-b+std::sqrt(disc);
    return t>=0&&t<maximum?t:maximum;
}
Vec3 nextTrafficTarget(const World& world,const Vehicle& vehicle,uint32_t seed){
    const float grid=roadGrid(vehicle.position);
    Vec3 intersection{std::round(vehicle.position.x/grid)*grid,0,std::round(vehicle.position.z/grid)*grid};
    int heading=int(std::round(vehicle.yaw/(Pi*.5f)))&3;
    const int choice=int(hash32(seed)%7u);
    if(choice==0)heading=(heading+1)&3;else if(choice==1)heading=(heading+3)&3;
    const float angle=float(heading)*Pi*.5f;
    Vec3 target=intersection+forward(angle)*grid+right(angle)*Lane;
    if(world.biome(target.x,target.z)==Biome::Ocean||world.biome(target.x,target.z)==Biome::Wetland){
        target=intersection-forward(angle)*grid-right(angle)*Lane;
    }
    return atGround(world,target);
}
Vec3 pedestrianCorner(const World& world,Vec3 position,uint32_t seed){
    const float grid=roadGrid(position);const int corner=int(hash32(seed)&3u);
    const float bx=std::floor(position.x/grid)*grid,bz=std::floor(position.z/grid)*grid;
    return atGround(world,{bx+((corner&1)?grid-12.0f:12.0f),0,bz+((corner&2)?grid-12.0f:12.0f)});
}
struct SaveWriter {
    std::vector<uint8_t> data;
    void u32(uint32_t n){for(int k=0;k<4;++k)data.push_back(uint8_t(n>>(k*8)));}
    void integer(int n){u32(uint32_t(n));}
    void real(float n){uint32_t bits=0;std::memcpy(&bits,&n,4);u32(bits);}
    void vector(Vec3 p){real(p.x);real(p.y);real(p.z);}
};
struct SaveReader {
    const std::vector<uint8_t>& data;size_t offset=0;bool good=true;
    uint32_t u32(){if(offset+4>data.size()){good=false;return 0;}uint32_t n=0;for(int k=0;k<4;++k)n|=uint32_t(data[offset++])<<(k*8);return n;}
    int integer(){uint32_t n=u32();int32_t s=0;std::memcpy(&s,&n,4);return s;}
    float real(){uint32_t bits=u32();float n=0;std::memcpy(&n,&bits,4);if(!finite(n))good=false;return n;}
    Vec3 vector(){float x=real(),y=real(),z=real();return {x,y,z};}
};
uint32_t crc32(const uint8_t* bytes,size_t size){uint32_t crc=0xffffffffu;for(size_t i=0;i<size;++i){crc^=bytes[i];for(int b=0;b<8;++b)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}return ~crc;}
std::filesystem::path utf8Path(const std::string& value){
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(value.begin(),value.end()));
#else
    return std::filesystem::u8path(value);
#endif
}
bool validPosition(Vec3 p){return finite(p)&&std::abs(p.x)<=World::Extent&&std::abs(p.z)<=World::Extent&&p.y>=-100&&p.y<=4096;}
}

const std::vector<Mission>& Game::missions(){
    static const std::vector<Mission> list{
        {"A FAVOR IN LOW TIDE","Inez: My sedan is by the curb. Bring it to the harbor steps. Keep the paint on it.",{12,0,24},{268,0,128},400},
        {"LAST LIGHT DISPATCH","Inez: The clinic's battery shipment never arrived. Collect it at the north arcade, then find Mara in Westhaven.",{268,0,128},{12,0,512},850},
        {"THE LONG WAY HOME","Mara: These records expose the harbor extortion racket. Take a car, shake the patrol, and meet me south of town.",DispatchDestination,{12,0,-512},1200},
        {"SIGNAL ON THE WATER","Mara: Our witness left two recordings along the eastern promenade. Find them on foot; let the city hear the truth.",{12,0,-512},{780,0,-512},1600}
    };
    return list;
}
void Game::initialize(){
    *this=Game{};
    player=atGround(world,player);world.stream(player);
    vehicles.reserve(56);pedestrians.reserve(88);
    auto addCar=[&](Vec3 p,float angle,Vec3 color,bool parked,bool police,VehicleKind kind){Vehicle v;v.position=atGround(world,p);v.yaw=angle;v.color=color;v.parked=parked;v.police=police;v.kind=kind;vehicles.push_back(v);};
    addCar({Lane,0,8},0,{.035f,.42f,.46f},true,false,VehicleKind::Car);
    addCar({Lane,0,-18},0,{.67f,.13f,.065f},true,false,VehicleKind::Motorcycle);
    for(int i=0;i<6;++i){float x=float((i%3)-1)*128+Lane,z=float((i/3)*256-128)+23;addCar({x,0,z},0,{.15f+random01(uint32_t(i)*17)*.55f,.13f+random01(uint32_t(i)*13)*.50f,.12f+random01(uint32_t(i)*23)*.50f},true,false,VehicleKind::Car);}
    for(int i=0;i<4;++i)addCar({float((i&1)?128:-128)+Lane,0,float((i&2)?128:-128)+24},0,{.09f,.12f,.16f},true,true,VehicleKind::Car);
    for(int i=0;i<36;++i){
        const int h=i&3;float angle=float(h)*Pi*.5f;Vec3 p{float((i%5)-2)*128,0,float(((i/5)%5)-2)*128};p+=forward(angle)*(26+float(i%4)*19)+right(angle)*Lane;
        addCar(p,angle,{.10f+random01(uint32_t(i)+55)*.60f,.10f+random01(uint32_t(i)+77)*.60f,.10f+random01(uint32_t(i)+111)*.60f},false,false,i%9==7?VehicleKind::Motorcycle:VehicleKind::Car);
        vehicles.back().speed=7+random01(uint32_t(i)+211)*6;
    }
    for(size_t i=0;i<vehicles.size();++i)trafficTargets.push_back(nextTrafficTarget(world,vehicles[i],uint32_t(i)*719));
    for(int i=0;i<84;++i){
        Pedestrian p;
        if(i<4)p.position=vehicles[size_t(i)+8].position+Vec3{8,0,0};
        else {int block=i-4;float x=float((block%5)-2)*128+12,z=float(((block/5)%5)-2)*128+12;float walk=float((block*31)%100);if(block&1)x+=walk;else z+=walk;p.position={x,0,z};}
        p.position=atGround(world,p.position);p.phase=random01(uint32_t(i))*2*Pi;p.yaw=random01(uint32_t(i)+7)*2*Pi;pedestrians.push_back(p);pedestrianTargets.push_back(pedestrianCorner(world,p.position,uint32_t(i)*37));
    }
    message="MERIDIAN COAST  /  Walk to the amber marker. Press M to meet Inez.  E enters a vehicle.";messageTime=13;
}
const Mission* Game::missionInfo() const {
    int index=activeMission>=0?activeMission:completedMissions;
    return index>=0&&index<int(missions().size())?&missions()[size_t(index)]:nullptr;
}
Vec3 Game::missionTarget() const {
    const Mission* mission=missionInfo();if(!mission)return Garage;
    if(activeMission<0)return mission->start;
    if(activeMission==1&&missionStage>=1)return DispatchDestination;
    if(activeMission==3&&missionStage>=1)return SignalDestination;
    return mission->target;
}
Vec3 Game::cameraEye() const {
    const Vec3 anchor=player+Vec3{0,occupied>=0?1.65f:1.45f,0};
    const Vec3 direction=forward(yaw)*std::cos(pitch)+Vec3{0,-std::sin(pitch),0};
    float distance=occupied>=0?8.0f:(aiming?3.0f:5.5f);
    Vec3 desired=anchor-direction*distance+right(yaw)*(aiming?.67f:.32f);
    Vec3 delta=desired-anchor;float maximum=length(delta);Vec3 ray=normalized(delta);
    for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids){Box expanded{box.min-Vec3{.20f,.20f,.20f},box.max+Vec3{.20f,.20f,.20f}};maximum=std::min(maximum,rayBox(anchor,ray,expanded,maximum));}
    Vec3 result=anchor+ray*std::max(.35f,maximum-.15f);
    result.y=std::max(result.y,world.height(result.x,result.z)+.35f);
    return result;
}
Vec3 Game::cameraTarget() const {
    Vec3 direction=forward(yaw)*std::cos(pitch)+Vec3{0,-std::sin(pitch),0};
    return player+Vec3{0,occupied>=0?1.65f:1.45f,0}+direction*35;
}
void Game::update(const Input& input,float elapsed){
    if(paused||!finite(elapsed)||elapsed<=0)return;
    const float dt=std::min(elapsed,.05f);
    Input in=input;
    in.moveX=finite(in.moveX)?clamp(in.moveX,-1,1):0;in.moveY=finite(in.moveY)?clamp(in.moveY,-1,1):0;
    in.lookX=finite(in.lookX)?clamp(in.lookX,-1,1):0;in.lookY=finite(in.lookY)?clamp(in.lookY,-1,1):0;
    const bool interact=in.interact&&!wasInteract,reload=in.reload&&!wasReload,missionAction=in.mission&&!wasMission,radio=in.radio&&!wasRadio,jump=in.jump&&!wasJump;
    wasInteract=in.interact;wasReload=in.reload;wasMission=in.mission;wasRadio=in.radio;wasJump=in.jump;
    ++simulationTick;time+=dt;dayTime=std::fmod(dayTime+dt/75.0f,24.0f);
    rain=towards(rain,clamp((std::sin(time*.004f-1.5f)+.10f)*1.25f,0,1),dt*.018f);
    fireCooldown=std::max(0.0f,fireCooldown-dt);shotFlash=std::max(0.0f,shotFlash-dt);invulnerabilityTimer=std::max(0.0f,invulnerabilityTimer-dt);messageTime=std::max(0.0f,messageTime-dt);
    yaw=wrapAngle(yaw+in.lookX);pitch=clamp(pitch+in.lookY,-.85f,1.12f);aiming=in.aim&&occupied<0;
    if(std::abs(in.lookX)+std::abs(in.lookY)>.001f)cameraFollowDelay=2.4f;else cameraFollowDelay=std::max(0.0f,cameraFollowDelay-dt);
    if(occupied>=int(vehicles.size())||occupied< -1)occupied=-1;
    world.stream(player);
    if(trafficTargets.size()!=vehicles.size()){trafficTargets.clear();for(size_t i=0;i<vehicles.size();++i)trafficTargets.push_back(nextTrafficTarget(world,vehicles[i],uint32_t(i)));}
    if(pedestrianTargets.size()!=pedestrians.size()){pedestrianTargets.clear();for(size_t i=0;i<pedestrians.size();++i)pedestrianTargets.push_back(pedestrianCorner(world,pedestrians[i].position,uint32_t(i)));}
    if(radio){radioStation=(radioStation+1)%4;static const char* names[]={"Radio off","TIDELINE FM","NIGHT WINDOW","ION DRIVE"};message=names[radioStation];messageTime=4;}
    if(interact){
        if(occupied>=0){
            Vehicle& v=vehicles[size_t(occupied)];
            if(std::abs(v.speed)>8){message="Slow below 30 km/h before getting out.";messageTime=3;}
            else {
                bool found=false;
                for(Vec3 offset:std::array<Vec3,4>{{{-2.0f,0,0},{2.0f,0,0},{0,0,-3.0f},{0,0,3.0f}}}){
                    Vec3 exit=atGround(world,v.position+rotate(offset,v.yaw));
                    if(!world.blocked(exit,.35f)){player=exit;found=true;break;}
                }
                if(found){v.parked=true;v.speed=0;v.velocity={};occupied=-1;verticalSpeed=0;grounded=true;message="On foot. Hold right mouse to aim; left mouse fires.";messageTime=4;}
                else {message="Both doors are obstructed. Move the vehicle to open ground.";messageTime=3;}
            }
        }else{
            float nearest=6.0f;int index=-1;
            for(size_t i=0;i<vehicles.size();++i)if(vehicles[i].health>0&&std::abs(vehicles[i].speed)<6){float distance=planarDistance(player,vehicles[i].position);if(distance<nearest){nearest=distance;index=int(i);}}
            if(index>=0){occupied=index;Vehicle& v=vehicles[size_t(index)];v.parked=false;player=v.position;verticalSpeed=0;grounded=true;reloadTimer=0;cameraFollowDelay=0;yaw=v.yaw;
                if(v.police){wanted=std::max(wanted,2);wantedTimer=28;message="Patrol vehicle taken. Dispatch has your description.";}else message=v.kind==VehicleKind::Motorcycle?"Motorcycle  /  W/S throttle and reverse  A/D steer  Space brake":"W/S throttle and reverse  A/D steer  Space handbrake  E exit  Q radio";
                messageTime=5;
            }else {message="Move beside a stopped vehicle and press E.";messageTime=3;}
        }
    }
    if(occupied<0){
        Vec3 move=forward(yaw)*in.moveY+right(yaw)*in.moveX;float amount=std::min(1.0f,length(move));move=normalized(move)*amount;
        float speed=aiming?2.2f:(in.sprint?7.3f:3.9f);
        Vec3 delta=move*(speed*dt);Vec3 before=player;player=world.move(player,delta,.34f);
        const float ground=world.height(player.x,player.z);
        if(jump&&grounded){verticalSpeed=5.8f;grounded=false;}
        verticalSpeed-=18*dt;player.y+=verticalSpeed*dt;
        if(player.y<=ground){player.y=ground;verticalSpeed=0;grounded=true;}
        playerMotion=towards(playerMotion,amount,dt*7);playerPhase+=planarDistance(player,before)*2.5f;
    }else {
        Vehicle& v=vehicles[size_t(occupied)];const bool bike=v.kind==VehicleKind::Motorcycle;
        v.steer=lerp(v.steer,in.moveX,std::min(1.0f,dt*8));
        const float throttle=v.health>0?in.moveY:0;
        const float engine=(bike?15.5f:12.2f)*(v.health>25?1.0f:.55f);
        float acceleration=throttle*engine-v.speed*(.12f+.0055f*std::abs(v.speed));
        if(throttle*v.speed<0)acceleration=throttle*23.0f;
        v.speed=clamp(v.speed+acceleration*dt,bike?-12.0f:-11.0f,bike?57.0f:48.0f);
        if(in.brake)v.speed=towards(v.speed,0,dt*30);
        if(std::abs(throttle)<.01f&&std::abs(v.speed)<.4f)v.speed=towards(v.speed,0,dt*.8f);
        const float steering=(bike?.61f:.52f)/(1+std::abs(v.speed)*.030f);
        v.yaw=wrapAngle(v.yaw+v.steer*steering*v.speed*dt/(bike?1.85f:2.75f));
        Vec3 desired=forward(v.yaw)*v.speed;
        v.velocity=lerp(v.velocity,desired,std::min(1.0f,dt*(in.brake?2.0f:(bike?9.0f:6.0f))));
        const Vec3 before=v.position,delta=v.velocity*dt;v.position=world.move(v.position,delta,bike?.45f:1.02f);v.position=atGround(world,v.position);
        float moved=planarDistance(before,v.position),expected=length(delta);
        if(expected>.025f&&moved<expected*.48f){float impact=std::abs(v.speed);v.health=std::max(0.0f,v.health-std::max(0.0f,impact-4)*.75f);health=std::max(0.0f,health-std::max(0.0f,impact-20)*.17f);v.speed*=-.16f;v.velocity=forward(v.yaw)*v.speed;}
        player=v.position;playerMotion=0;
        if(cameraFollowDelay<=0&&std::abs(v.speed)>1.4f)yaw=wrapAngle(yaw+wrapAngle(v.yaw-yaw)*std::min(1.0f,dt*2.4f));
    }
    player.x=clamp(player.x,-World::Extent+2,World::Extent-2);player.z=clamp(player.z,-World::Extent+2,World::Extent-2);
    if(occupied>=0)vehicles[size_t(occupied)].position=player;
    if(reload&&occupied<0&&ammo<Magazine&&reserveAmmo>0&&reloadTimer<=0){reloadTimer=1.35f;message="Reloading...";messageTime=1.35f;}
    if(reloadTimer>0){reloadTimer-=dt;if(reloadTimer<=0){reloadTimer=0;int rounds=std::min(Magazine-ammo,reserveAmmo);ammo+=rounds;reserveAmmo-=rounds;}}
    if(in.fire&&occupied<0&&fireCooldown<=0&&reloadTimer<=0){
        if(ammo>0){
            --ammo;fireCooldown=.145f;shotFlash=.07f;
            const Vec3 shoulder=player+Vec3{0,1.35f,0};
            Vec3 origin=player+Vec3{0,1.31f,0}+forward(yaw)*.74f+right(yaw)*.13f;
            const Vec3 toMuzzle=origin-shoulder;
            float clearance=length(toMuzzle);
            for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids)clearance=std::min(clearance,rayBox(shoulder,normalized(toMuzzle),box,clearance));
            origin=shoulder+normalized(toMuzzle)*std::max(0.0f,clearance-.025f);
            Vec3 direction=forward(yaw);
            if(aiming){
                const Vec3 eye=cameraEye(),sight=normalized(cameraTarget()-eye);
                float aimDistance=140.0f;
                for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids)aimDistance=std::min(aimDistance,rayBox(eye,sight,box,aimDistance));
                for(const Pedestrian& p:pedestrians)if(p.health>0)aimDistance=std::min(aimDistance,raySphere(eye,sight,p.position+Vec3{0,1.0f,0},.56f,aimDistance));
                for(const Vehicle& v:vehicles)aimDistance=std::min(aimDistance,raySphere(eye,sight,v.position+Vec3{0,.85f,0},1.2f,aimDistance));
                direction=normalized(eye+sight*aimDistance-origin);
            }
            shotOrigin=origin;
            float hit=140.0f;int person=-1,car=-1;
            for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids)hit=std::min(hit,rayBox(origin,direction,box,hit));
            for(size_t i=0;i<pedestrians.size();++i){const Pedestrian& p=pedestrians[i];if(p.health<=0)continue;float t=raySphere(origin,direction,p.position+Vec3{0,1.0f,0},.56f,hit);if(t<hit){hit=t;person=int(i);car=-1;}}
            for(size_t i=0;i<vehicles.size();++i){float t=raySphere(origin,direction,vehicles[i].position+Vec3{0,.85f,0},1.2f,hit);if(t<hit){hit=t;person=-1;car=int(i);}}
            if(person>=0){Pedestrian& p=pedestrians[size_t(person)];p.health=std::max(0.0f,p.health-40);p.panic=14;wanted=std::max(wanted,person<4?3:2);}
            if(car>=0){Vehicle& v=vehicles[size_t(car)];v.health=std::max(0.0f,v.health-12);if(v.police)wanted=std::max(wanted,3);}
            shotEnd=origin+direction*hit;wanted=std::max(wanted,1);wantedTimer=22+float(wanted)*5;
            for(Pedestrian& p:pedestrians)if(planarDistance(p.position,player)<80)p.panic=12;
        }else {fireCooldown=.3f;message=reserveAmmo>0?"Magazine empty. Press R to reload.":"Out of ammunition. Visit the outfitter west of the harbor.";messageTime=3;}
    }
    bool policeSight=false;
    for(size_t i=0;i<vehicles.size();++i){
        if(int(i)==occupied)continue;
        Vehicle& v=vehicles[i];
        if(v.health<=0){v.speed=towards(v.speed,0,dt*8);continue;}
        float targetSpeed=0;
        Vec3 target=trafficTargets[i];
        if(v.police&&wanted>0){
            v.parked=false;float distance=planarDistance(v.position,player);bool hasSight=false;
            if(distance<95){
                Vec3 ray=normalized(player+Vec3{0,1,0}-(v.position+Vec3{0,1,0}));float visible=distance;
                for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids)visible=std::min(visible,rayBox(v.position+Vec3{0,1,0},ray,box,visible));
                if(visible>=distance-.5f){policeSight=true;hasSight=true;}
            }
            if(distance<28)target=player;
            else {float grid=roadGrid(v.position);Vec3 node{std::round(v.position.x/grid)*grid,0,std::round(v.position.z/grid)*grid};Vec3 delta=player-node;if(std::abs(delta.x)>std::abs(delta.z))target=node+Vec3{delta.x>0?grid:-grid,0,Lane};else target=node+Vec3{Lane,0,delta.z>0?grid:-grid};}
            targetSpeed=distance<8?3.0f:22+float(wanted)*2;
            if(hasSight&&distance<6&&occupied<0&&invulnerabilityTimer<=0){health=std::max(0.0f,health-dt*(7+float(wanted)*3));}
        }else if(v.parked){v.speed=0;v.velocity={};continue;}
        else {
            if(planarDistance(v.position,target)<11){trafficTargets[i]=nextTrafficTarget(world,v,uint32_t(i)*391+simulationTick);target=trafficTargets[i];}
            targetSpeed=10+random01(uint32_t(i)*193)*7;
            if(wanted==0&&v.police)targetSpeed=9;
        }
        Vec3 direction=target-v.position;direction.y=0;float desiredYaw=std::atan2(direction.x,direction.z),turn=wrapAngle(desiredYaw-v.yaw);
        if(std::abs(turn)>.7f)targetSpeed=std::min(targetSpeed,6.0f);
        for(size_t j=0;j<vehicles.size();++j){if(i==j)continue;Vec3 offset=vehicles[j].position-v.position;float ahead=dot(offset,forward(v.yaw));if(ahead>0&&ahead<10+std::abs(v.speed)*.65f&&std::abs(dot(offset,right(v.yaw)))<2.3f)targetSpeed=std::min(targetSpeed,std::max(0.0f,(ahead-5)*.75f));}
        if(occupied<0){Vec3 offset=player-v.position;if(dot(offset,forward(v.yaw))>0&&planarDistance(player,v.position)<10&&std::abs(dot(offset,right(v.yaw)))<2)targetSpeed=v.police&&wanted>0?2.0f:0.0f;}
        v.speed=towards(v.speed,targetSpeed,dt*(targetSpeed<v.speed?8:3.8f));v.steer=clamp(turn*1.5f,-1,1);
        v.yaw=wrapAngle(v.yaw+clamp(turn,-1.2f*dt,1.2f*dt));v.velocity=forward(v.yaw)*v.speed;
        Vec3 before=v.position;v.position=world.move(v.position,v.velocity*dt,v.kind==VehicleKind::Motorcycle?.45f:1.0f);v.position=atGround(world,v.position);
        if(planarDistance(before,v.position)<.002f&&v.speed>1){v.speed=0;trafficTargets[i]=nextTrafficTarget(world,v,uint32_t(i)+simulationTick+827);}
    }
    if(occupied>=0){
        Vehicle& driven=vehicles[size_t(occupied)];
        for(size_t i=0;i<vehicles.size();++i){if(int(i)==occupied)continue;Vehicle& other=vehicles[i];float radius=driven.kind==VehicleKind::Motorcycle?1.55f:2.15f;Vec3 delta=driven.position-other.position;delta.y=0;float distance=length(delta);
            if(distance>0.01f&&distance<radius){float impact=std::abs(driven.speed-other.speed);Vec3 normal=delta/distance;driven.position=world.move(driven.position,normal*(radius-distance),.8f);other.position=world.move(other.position,-normal*(radius-distance)*.5f,.8f);driven.speed*=.65f;other.speed*=.6f;driven.health=std::max(0.0f,driven.health-std::max(0.0f,impact-3)*.25f);other.health=std::max(0.0f,other.health-std::max(0.0f,impact-3)*.3f);if(other.police&&impact>2){wanted=std::max(wanted,2);wantedTimer=25;}}
        }
        player=driven.position;
    }
    for(size_t i=0;i<pedestrians.size();++i){
        Pedestrian& p=pedestrians[i];if(p.health<=0)continue;
        p.panic=std::max(0.0f,p.panic-dt);Vec3 target=pedestrianTargets[i];float speed=1.0f+random01(uint32_t(i)*37)*.65f;
        if(i<4){
            if(wanted>0&&planarDistance(p.position,player)<85){
                target=player;speed=3.7f;
                const Vec3 eye=p.position+Vec3{0,1.55f,0},toPlayer=player+Vec3{0,1.1f,0}-eye;
                const float distance=length(toPlayer);float visible=distance;
                for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids)visible=std::min(visible,rayBox(eye,normalized(toPlayer),box,visible));
                if(visible>=distance-.1f){policeSight=true;if(distance<15){speed=0;if(invulnerabilityTimer<=0)health=std::max(0.0f,health-dt*(2.0f+float(wanted)));}}
            }
            else if(i+8<vehicles.size()){target=vehicles[i+8].position+Vec3{8,0,0};speed=1.3f;}
        }else if(p.panic>0){target=p.position+normalized(p.position-player)*15;speed=4.4f;}
        else if(planarDistance(p.position,target)<1.0f){
            float grid=roadGrid(p.position);float bx=std::floor(p.position.x/grid)*grid,bz=std::floor(p.position.z/grid)*grid;
            bool east=p.position.x-bx>grid*.5f,north=p.position.z-bz>grid*.5f;
            // Walk one edge of the block at a time, never diagonally through its buildings.
            if(east&&!north)target={bx+grid-12,0,bz+grid-12};else if(east&&north)target={bx+12,0,bz+grid-12};else if(!east&&north)target={bx+12,0,bz+12};else target={bx+grid-12,0,bz+12};
            pedestrianTargets[i]=atGround(world,target);
        }
        Vec3 delta=target-p.position;delta.y=0;float distance=length(delta);
        if(distance>.25f&&speed>0){Vec3 direction=delta/distance;p.yaw=wrapAngle(p.yaw+wrapAngle(std::atan2(direction.x,direction.z)-p.yaw)*std::min(1.0f,dt*7));Vec3 before=p.position;p.position=world.move(p.position,direction*(std::min(distance,speed*dt)),.28f);p.position=atGround(world,p.position);p.phase+=planarDistance(p.position,before)*3;}
        for(size_t j=0;j<vehicles.size();++j){Vehicle& v=vehicles[j];if(std::abs(v.speed)<2.5f)continue;float distanceToCar=planarDistance(p.position,v.position);if(distanceToCar<2){p.health=std::max(0.0f,p.health-std::abs(v.speed)*7);p.position=world.move(p.position,normalized(p.position-v.position)*2,.28f);p.panic=12;if(int(j)==occupied){wanted=std::max(wanted,i<4?3:2);wantedTimer=32;v.speed*=.84f;}}else if(distanceToCar<7&&i>=4)p.panic=std::max(p.panic,3.0f);}
    }
    if(wanted>0){
        if(policeSight)wantedTimer=std::max(wantedTimer,8.0f);
        else wantedTimer-=dt;
        if(wantedTimer<=0){--wanted;wantedTimer=wanted>0?12.0f:0;if(wanted==0){message="The search has ended. You are clear.";messageTime=5;}}
    }
    populationTimer+=dt;
    if(populationTimer>1){
        populationTimer=0;
        for(size_t i=8;i<vehicles.size();++i){
            Vehicle& v=vehicles[i];if(int(i)==occupied||planarDistance(v.position,player)<430)continue;
            float grid=roadGrid(player);float angle=float(hash32(uint32_t(i)+simulationTick)%4u)*Pi*.5f;Vec3 node{std::round(player.x/grid)*grid,0,std::round(player.z/grid)*grid};Vec3 p=node+forward(angle)*grid*2+right(angle)*Lane;
            if(world.biome(p.x,p.z)==Biome::Ocean)continue;
            v.position=atGround(world,p);v.yaw=wrapAngle(angle+Pi);v.speed=v.police?0.0f:8.0f;v.health=100;v.parked=v.police&&wanted==0;trafficTargets[i]=nextTrafficTarget(world,v,uint32_t(i)+simulationTick);
        }
        for(size_t i=4;i<pedestrians.size();++i){Pedestrian& p=pedestrians[i];if(planarDistance(p.position,player)<330)continue;float grid=roadGrid(player);uint32_t h=hash32(uint32_t(i)+simulationTick);Vec3 base{std::floor(player.x/grid)*grid+float(int(h%5u)-2)*grid,0,std::floor(player.z/grid)*grid+float(int((h>>4)%5u)-2)*grid};base+=Vec3{12,0,12};if(world.biome(base.x,base.z)==Biome::Ocean)continue;p.position=atGround(world,base);p.health=100;p.panic=0;pedestrianTargets[i]=pedestrianCorner(world,p.position,h);}
    }
    if(missionAction){
        if(activeMission<0&&completedMissions<int(missions().size())&&planarDistance(player,missions()[size_t(completedMissions)].start)<14){activeMission=completedMissions;missionStage=0;missionTimer=activeMission==1?180.0f:(activeMission==2?240.0f:0.0f);message=missions()[size_t(activeMission)].briefing;messageTime=12;}
        else if(planarDistance(player,Garage)<16){if(money>=75){money-=75;health=100;if(occupied>=0)vehicles[size_t(occupied)].health=100;message="Harbor garage  /  repaired and treated  -$75";}else message="Harbor garage  /  repairs cost $75";messageTime=5;}
        else if(planarDistance(player,Outfitter)<16){if(money>=60&&reserveAmmo<=9910){money-=60;reserveAmmo+=90;message="Outfitter  /  90 rounds  -$60";}else message="Outfitter  /  ammunition costs $60";messageTime=5;}
        else if(activeMission<0){message=completedMissions<int(missions().size())?"Meet your contact at the amber marker to begin the next job.":"The harbor story is complete. Explore the coast, ride the city, or visit the garage and outfitter.";messageTime=5;}
        else {message=missions()[size_t(activeMission)].briefing;messageTime=8;}
    }
    if(activeMission>=0){
        bool complete=false,failed=false;const float distance=planarDistance(player,missionTarget());const float speed=occupied>=0?std::abs(vehicles[size_t(occupied)].speed):0;
        if(missionTimer>0){missionTimer=std::max(0.0f,missionTimer-dt);if(missionTimer==0)failed=true;}
        switch(activeMission){
        case 0:
            if(missionStage==0&&occupied>=0){missionStage=1;message="Inez: That's the one. Bring it to the harbor steps, east along the avenue.";messageTime=7;}
            if(missionStage==1&&occupied>=0&&distance<17&&speed<4)complete=true;
            break;
        case 1:
            if(missionStage==0&&distance<16&&occupied>=0&&speed<4){missionStage=1;missionTimer=130;message="Batteries loaded. Mara is waiting in Westhaven. Deliver before the clinic loses power.";messageTime=9;}
            else if(missionStage==1&&distance<16&&occupied>=0&&speed<4)complete=true;
            break;
        case 2:
            if(missionStage==0&&occupied>=0){missionStage=1;wanted=std::max(wanted,2);wantedTimer=22;message="Dispatch intercepted the call. Lose the patrols, then meet Mara south of the canal.";messageTime=9;}
            if(missionStage==1&&distance<24){missionStage=2;message=wanted>0?"Mara: Don't lead them here. Break line of sight, then circle back.":"Mara: You're clear. Bring the records to me.";messageTime=8;}
            if(missionStage==2&&wanted==0&&distance<30)complete=true;
            break;
        case 3:
            if(missionStage==0&&distance<10&&occupied<0){missionStage=1;message="Recording 1: The harbor fees paid for private patrols. The second recording is north on the promenade.";messageTime=10;}
            else if(missionStage==1&&distance<10&&occupied<0)complete=true;
            break;
        default:failed=true;break;
        }
        if(complete){const Mission& m=missions()[size_t(activeMission)];money=std::min(100000000,money+m.reward);message=std::string("JOB COMPLETE  /  ")+m.title+"  +$"+std::to_string(m.reward);if(activeMission==3)message+="  /  Mara: The recordings are on the air. The city gets to decide what happens next.";messageTime=12;++completedMissions;activeMission=-1;missionStage=0;missionTimer=0;health=std::min(100.0f,health+20);}
        else if(failed){message="Job expired. Return to your contact to try again.";messageTime=7;activeMission=-1;missionStage=0;missionTimer=0;}
    }
    if(health<=0||player.y< -25){
        health=100;money=std::max(0,money-100);wanted=0;wantedTimer=0;occupied=-1;activeMission=-1;missionStage=0;missionTimer=0;player=atGround(world,{12,0,12});yaw=0;pitch=.2f;verticalSpeed=0;grounded=true;invulnerabilityTimer=5;
        message="Recovered at Harbor Clinic. Treatment -$100. Your completed jobs and possessions are safe.";messageTime=9;
    }else if(wanted==0&&health<35)health=std::min(35.0f,health+dt*1.5f);
    world.stream(player);
}
bool Game::save(const std::string& path) const {
    try {
        if(path.empty()||vehicles.size()>256||pedestrians.size()>512)return false;
        SaveWriter payload;
        payload.vector(player);payload.real(yaw);payload.real(pitch);payload.real(health);payload.integer(money);payload.integer(ammo);payload.integer(reserveAmmo);payload.integer(wanted);
        payload.integer(occupied);payload.integer(activeMission);payload.integer(missionStage);payload.integer(completedMissions);payload.integer(radioStation);
        payload.real(time);payload.real(dayTime);payload.real(rain);payload.real(missionTimer);payload.real(wantedTimer);payload.real(reloadTimer);payload.real(verticalSpeed);payload.u32(grounded?1u:0u);
        payload.u32(uint32_t(vehicles.size()));
        for(const Vehicle& v:vehicles){payload.vector(v.position);payload.real(v.yaw);payload.real(v.speed);payload.real(v.steer);payload.vector(v.velocity);payload.vector(v.color);payload.integer(int(v.kind));payload.u32(v.police?1u:0u);payload.u32(v.parked?1u:0u);payload.real(v.health);}
        payload.u32(uint32_t(pedestrians.size()));
        for(const Pedestrian& p:pedestrians){payload.vector(p.position);payload.real(p.yaw);payload.real(p.phase);payload.real(p.panic);payload.real(p.health);}
        SaveWriter header;for(char c:std::string("MCSTSAVE"))header.data.push_back(uint8_t(c));header.u32(1);header.u32(uint32_t(payload.data.size()));header.u32(crc32(payload.data.data(),payload.data.size()));
        const std::filesystem::path destination=utf8Path(path);
        std::filesystem::path temporary=destination;temporary+=".tmp";
        if(!destination.parent_path().empty())std::filesystem::create_directories(destination.parent_path());
        {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file)return false;file.write(reinterpret_cast<const char*>(header.data.data()),std::streamsize(header.data.size()));file.write(reinterpret_cast<const char*>(payload.data.data()),std::streamsize(payload.data.size()));file.flush();if(!file){file.close();std::error_code ignored;std::filesystem::remove(temporary,ignored);return false;}}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ignored;std::filesystem::remove(temporary,ignored);return false;}
#else
        int fd=::open(temporary.c_str(),O_RDONLY);if(fd<0)return false;const bool flushed=::fsync(fd)==0;::close(fd);if(!flushed)return false;
        std::filesystem::rename(temporary,destination);
#endif
        return true;
    }catch(...){return false;}
}
bool Game::load(const std::string& path){
    try {
        std::ifstream file(utf8Path(path),std::ios::binary|std::ios::ate);if(!file)return false;std::streamoff size=file.tellg();if(size<20||size>1024*1024)return false;
        std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);if(!file)return false;
        const uint8_t magic[8]={'M','C','S','T','S','A','V','E'};
        if(std::memcmp(bytes.data(),magic,8)!=0)return false;
        SaveReader header{bytes,8};if(header.u32()!=1)return false;uint32_t payloadSize=header.u32(),checksum=header.u32();if(payloadSize!=bytes.size()-20)return false;if(crc32(bytes.data()+20,payloadSize)!=checksum)return false;
        Game state;SaveReader reader{bytes,20};
        state.player=reader.vector();state.yaw=reader.real();state.pitch=reader.real();state.health=reader.real();state.money=reader.integer();state.ammo=reader.integer();state.reserveAmmo=reader.integer();state.wanted=reader.integer();
        state.occupied=reader.integer();state.activeMission=reader.integer();state.missionStage=reader.integer();state.completedMissions=reader.integer();state.radioStation=reader.integer();
        state.time=reader.real();state.dayTime=reader.real();state.rain=reader.real();state.missionTimer=reader.real();state.wantedTimer=reader.real();state.reloadTimer=reader.real();state.verticalSpeed=reader.real();uint32_t ground=reader.u32();state.grounded=ground==1;
        if(!reader.good||!validPosition(state.player)||std::abs(state.yaw)>Pi*2||state.pitch<-.85f||state.pitch>1.12f||state.health<0||state.health>100||state.money<0||state.money>100000000||state.ammo<0||state.ammo>Magazine||state.reserveAmmo<0||state.reserveAmmo>10000||state.wanted<0||state.wanted>5||state.radioStation<0||state.radioStation>3)return false;
        if(state.completedMissions<0||state.completedMissions>int(missions().size())||state.activeMission< -1||state.activeMission>=int(missions().size())||state.missionStage<0||state.missionStage>2||(state.activeMission>=0&&state.activeMission!=state.completedMissions))return false;
        if((state.activeMission<0&&state.missionStage!=0)||(state.activeMission!=2&&state.missionStage>1)||state.time<0||state.time>1e9f||state.dayTime<0||state.dayTime>=24||state.rain<0||state.rain>1||state.missionTimer<0||state.missionTimer>10000||state.wantedTimer<0||state.wantedTimer>10000||state.reloadTimer<0||state.reloadTimer>2||std::abs(state.verticalSpeed)>100||ground>1)return false;
        uint32_t vehicleCount=reader.u32();if(vehicleCount>256)return false;state.vehicles.reserve(vehicleCount);
        for(uint32_t i=0;i<vehicleCount;++i){Vehicle v;v.position=reader.vector();v.yaw=reader.real();v.speed=reader.real();v.steer=reader.real();v.velocity=reader.vector();v.color=reader.vector();int kind=reader.integer();uint32_t police=reader.u32(),parked=reader.u32();v.police=police==1;v.parked=parked==1;v.kind=VehicleKind(kind);v.health=reader.real();
            if(!reader.good||!validPosition(v.position)||std::abs(v.yaw)>Pi*2||std::abs(v.speed)>150||std::abs(v.steer)>1.01f||length(v.velocity)>200||v.color.x<0||v.color.x>1||v.color.y<0||v.color.y>1||v.color.z<0||v.color.z>1||kind<0||kind>3||police>1||parked>1||v.health<0||v.health>100)return false;
            state.vehicles.push_back(v);
        }
        if(state.occupied< -1||state.occupied>=int(state.vehicles.size()))return false;
        uint32_t pedestrianCount=reader.u32();if(pedestrianCount>512)return false;state.pedestrians.reserve(pedestrianCount);
        for(uint32_t i=0;i<pedestrianCount;++i){Pedestrian p;p.position=reader.vector();p.yaw=reader.real();p.phase=reader.real();p.panic=reader.real();p.health=reader.real();if(!reader.good||!validPosition(p.position)||std::abs(p.yaw)>Pi*2||std::abs(p.phase)>1e9f||p.panic<0||p.panic>10000||p.health<0||p.health>100)return false;state.pedestrians.push_back(p);}
        if(!reader.good||reader.offset!=bytes.size())return false;
        if(state.occupied>=0&&planarDistance(state.player,state.vehicles[size_t(state.occupied)].position)>3)return false;
        state.world.stream(state.player);
        for(size_t i=0;i<state.vehicles.size();++i)state.trafficTargets.push_back(nextTrafficTarget(state.world,state.vehicles[i],uint32_t(i)*719));
        for(size_t i=0;i<state.pedestrians.size();++i)state.pedestrianTargets.push_back(pedestrianCorner(state.world,state.pedestrians[i].position,uint32_t(i)*37));
        state.message="Save restored. Welcome back to Meridian Coast.";state.messageTime=5;
        *this=std::move(state);return true;
    }catch(...){return false;}
}
}
