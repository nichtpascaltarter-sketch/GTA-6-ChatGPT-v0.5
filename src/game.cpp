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
constexpr Vec3 Outfitter{-116,0,128};
constexpr Vec3 DispatchDestination{-372,0,256};
constexpr Vec3 SignalDestination{780,0,-384};
constexpr Vec3 LandingPier{2674,.40f,768};
constexpr Vec3 RescuePickup{3090,World::WaterLevel,1080};
constexpr Vec3 AirfieldStart{-3200,4,-1190};
constexpr Vec3 AirfieldStop{-3200,4,-1000};
// Survey point Y values are target heights above the terrain, not world elevations.
constexpr Vec3 SurveyPoints[]={{-3200,90,-500},{-2600,140,0},{-2700,110,800}};
constexpr float SurveyMinimum[]={50,90,60},SurveyMaximum[]={130,190,160};
constexpr Vec3 SplitContact{268,0,-172};
constexpr Vec3 SplitStart{259.2f,0,-180};
constexpr float SplitLimit=150;
constexpr int SplitPayout[]={0,150,350,650};
int splitMedal(float seconds){return seconds<=85?3:(seconds<=110?2:1);}
const Mission& splitDescription(){
    static const Mission activity{"HARBOR SPLIT","Rafi: Nine gates, one motorcycle. The city stays open. Damage costs five seconds; a medal pays once. Beat your own line on the next run.",SplitContact,SplitStart,0};
    return activity;
}
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
bool roadVehicle(VehicleKind kind){return kind==VehicleKind::Car||kind==VehicleKind::Motorcycle;}
bool insideServiceArea(Vec3 p,const Box& area,float floorHeight){
    return p.x>=area.min.x&&p.x<=area.max.x&&p.z>=area.min.z&&p.z<=area.max.z&&std::abs(p.y-floorHeight)<.4f;
}
void appendStarterCraft(std::vector<Vehicle>& vehicles,const World& world){
    bool boat=false,aircraft=false;
    for(const Vehicle& vehicle:vehicles){boat|=vehicle.kind==VehicleKind::Boat;aircraft|=vehicle.kind==VehicleKind::Aircraft;}
    if(!boat){Vehicle v;v.kind=VehicleKind::Boat;v.position={2678,World::WaterLevel,768};v.color={.08f,.40f,.47f};v.parked=true;vehicles.push_back(v);}
    if(!aircraft){Vehicle v;v.kind=VehicleKind::Aircraft;v.position={-3200,world.height(-3200,-1190),-1190};v.color={.79f,.18f,.065f};v.parked=true;vehicles.push_back(v);}
}
float boatSurface(Vec3 position,float time,float rain){return World::WaterLevel+(.035f+rain*.075f)*std::sin(time*1.8f+position.x*.06f)+.025f*std::sin(time*2.7f+position.z*.055f);}
bool boatDepth(const World& world,Vec3 p,float yaw){
    for(Vec3 offset:std::array<Vec3,5>{{{0,0,0},{0,0,2.6f},{0,0,-2.4f},{.90f,0,0},{-.90f,0,0}}}){Vec3 sample=p+rotate(offset,yaw);if(world.waterDepth(sample.x,sample.z)<.42f)return false;}
    return true;
}
bool provideLoan(World& world,std::vector<Vehicle>& vehicles,int occupied,VehicleKind kind){
    int loan=-1;
    for(size_t i=0;i<vehicles.size();++i)if(int(i)!=occupied&&vehicles[i].kind==kind){loan=int(i);break;}
    if(loan<0&&vehicles.size()>=256)return false;
    const Vec3 anchor=kind==VehicleKind::Boat?Vec3{2678,World::WaterLevel,768}:AirfieldStart;
    Vec3 position;bool found=false;
    for(Vec3 offset:std::array<Vec3,5>{{{0,0,0},{0,0,-18},{0,0,18},{0,0,-36},{0,0,36}}}){
        Vec3 candidate=anchor+offset;
        candidate.y=kind==VehicleKind::Boat?World::WaterLevel:world.height(candidate.x,candidate.z);
        if(kind==VehicleKind::Boat&&!boatDepth(world,candidate,0))continue;
        if(world.blocked(candidate,kind==VehicleKind::Boat?1.0f:4.8f))continue;
        bool occupiedSpace=false;
        for(size_t i=0;i<vehicles.size();++i)if(int(i)!=loan&&std::abs(vehicles[i].position.y-candidate.y)<2.0f&&planarDistance(vehicles[i].position,candidate)<(kind==VehicleKind::Boat?6.5f:9.0f)){occupiedSpace=true;break;}
        if(!occupiedSpace){position=candidate;found=true;break;}
    }
    if(!found)return false;
    Vehicle craft;craft.kind=kind;craft.position=position;craft.parked=true;
    craft.color=kind==VehicleKind::Boat?Vec3{.10f,.38f,.72f}:Vec3{.86f,.81f,.54f};
    if(loan>=0)vehicles[size_t(loan)]=craft;else vehicles.push_back(craft);
    return true;
}
int provideTrialBike(World& world,std::vector<Vehicle>& vehicles,int occupied){
    if(occupied>=0&&vehicles[size_t(occupied)].kind==VehicleKind::Motorcycle&&vehicles[size_t(occupied)].health>0)return occupied;
    int loan=-1;
    for(size_t i=0;i<vehicles.size();++i)if(int(i)!=occupied&&vehicles[i].kind==VehicleKind::Motorcycle){loan=int(i);break;}
    if(loan<0&&vehicles.size()>=256)return -1;
    for(float offset:{0.0f,-9.0f,9.0f,-18.0f,18.0f}){
        const Vec3 location=atGround(world,{263,0,-174+offset});
        if(world.blocked(location,.55f))continue;
        bool blocked=false;
        for(size_t i=0;i<vehicles.size();++i)if(int(i)!=loan&&planarDistance(location,vehicles[i].position)<3.2f&&std::abs(location.y-vehicles[i].position.y)<2){blocked=true;break;}
        if(blocked)continue;
        Vehicle bike;bike.kind=VehicleKind::Motorcycle;bike.position=location;bike.parked=true;bike.color={.49f,.12f,.69f};
        if(loan<0){vehicles.push_back(bike);return int(vehicles.size()-1);}
        vehicles[size_t(loan)]=bike;return loan;
    }
    return -1;
}
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
bool passengerPathClear(const World& world,Vec3 from,Vec3 to){
    // Passengers can step up onto a pier or threshold. Test the full standing
    // clearance on the step, across the gap, and down to the other landing.
    // Checking the vertical legs also prevents boarding through a ceiling.
    const float height=std::max(from.y,to.y);
    const std::array<Vec3,4> path{{from,{from.x,height,from.z},{to.x,height,to.z},to}};
    bool moved=false;
    for(size_t i=0;i+1<path.size();++i){
        const Vec3 delta=path[i+1]-path[i];const float distance=length(delta);
        if(distance<.001f)continue;
        moved=true;const Vec3 direction=delta/distance;
        for(const Chunk& chunk:world.chunks)for(const Box& obstacle:chunk.solids){
            const Box expanded{obstacle.min-Vec3{.35f,1.65f,.35f},obstacle.max+Vec3{.35f,-.04f,.35f}};
            if(rayBox(path[i],direction,expanded,distance)<distance-.001f)return false;
        }
    }
    return moved||!world.blocked(to,.35f);
}
float raySphere(Vec3 origin,Vec3 direction,Vec3 center,float radius,float maximum){
    Vec3 oc=origin-center;float b=dot(oc,direction),c=dot(oc,oc)-radius*radius,disc=b*b-c;
    if(disc<0)return maximum;
    float t=-b-std::sqrt(disc);if(t<0)t=-b+std::sqrt(disc);
    return t>=0&&t<maximum?t:maximum;
}
float simulateBoat(Vehicle& v,const World& world,const Input& input,float dt,float time,float rain){
    v.steer=lerp(v.steer,input.moveX,std::min(1.0f,dt*3.5f));
    v.throttle=v.health>0&&!input.brake?input.moveY:0;
    v.speed=clamp(v.speed+(v.throttle*5.4f-v.speed*(.075f+.0065f*std::abs(v.speed)))*dt,-6,24);
    if(input.brake)v.speed=towards(v.speed,0,dt*4.5f);
    v.yaw=wrapAngle(v.yaw+v.steer*v.speed*.038f/(1+std::abs(v.speed)*.022f)*dt);
    const Vec3 desired=forward(v.yaw)*v.speed;
    v.velocity.x=lerp(v.velocity.x,desired.x,std::min(1.0f,dt*1.9f));v.velocity.z=lerp(v.velocity.z,desired.z,std::min(1.0f,dt*1.9f));
    Vec3 delta{v.velocity.x*dt,0,v.velocity.z*dt};const Vec3 before=v.position;
    Vec3 candidate=world.move(before,delta,.95f);float impact=0;
    if(!boatDepth(world,candidate,v.yaw)||planarDistance(candidate,before)<length(delta)*.45f){
        if(length(delta)>.01f){impact=std::max(0.0f,std::abs(v.speed)-3);v.health=std::max(0.0f,v.health-impact*.8f);v.speed*=-.10f;v.velocity.x*=-.10f;v.velocity.z*=-.10f;}
        candidate=before;
    }
    const float surface=boatSurface(candidate,time,rain)+clamp(std::abs(v.speed)/24,0,1)*.08f;
    v.velocity.y=clamp(v.velocity.y+((surface-v.position.y)*22-v.velocity.y*7)*dt,-2,2);
    candidate.y=v.position.y+v.velocity.y*dt;v.position=candidate;
    v.pitch=lerp(v.pitch,.02f*std::sin(time*1.9f+v.position.z*.06f)+clamp(v.speed/24,0,1)*.060f,dt*3);
    v.roll=lerp(v.roll,v.steer*v.speed*.010f+(.015f+rain*.03f)*std::sin(time*1.6f+v.position.x*.08f),dt*2.5f);
    return impact;
}
float simulateAircraft(Vehicle& v,const World& world,const Input& input,float dt){
    float ground=world.height(v.position.x,v.position.z);
    const bool overWater=world.waterDepth(v.position.x,v.position.z)>.5f;
    if(overWater)ground=std::max(ground,World::WaterLevel);
    const bool onGround=v.position.y<=ground+.02f&&std::abs(v.velocity.y)<.3f;
    v.throttle=clamp(v.throttle+input.moveY*dt*.45f,0,1);if(v.health<=0)v.throttle=0;
    v.steer=lerp(v.steer,input.moveX,std::min(1.0f,dt*3));
    const float liftSpeed=std::max(15.0f,v.speed);
    const float trim=clamp((9.81f/(.0105f*liftSpeed*liftSpeed)-.35f)/4.5f,-.05f,.12f);
    float desiredPitch=trim+(input.sprint?.19f:0)-(input.brake&&!onGround?.19f:0);
    if(onGround)desiredPitch=input.sprint?.19f:0;
    v.pitch=lerp(v.pitch,desiredPitch,std::min(1.0f,dt*1.8f));
    v.roll=lerp(v.roll,onGround?0:v.steer*.68f,std::min(1.0f,dt*2.3f));
    if(onGround)v.yaw=wrapAngle(v.yaw+v.steer*std::min(v.speed,20.0f)*.032f*dt);
    else v.yaw=wrapAngle(v.yaw+9.81f*std::tan(v.roll)/std::max(v.speed,18.0f)*dt);
    const float flightPath=std::atan2(v.velocity.y,std::max(v.speed,1.0f)),angleOfAttack=v.pitch-flightPath;
    float liftCoefficient=clamp(.35f+4.5f*angleOfAttack,-.5f,1.5f);
    if(angleOfAttack>.32f)liftCoefficient*=clamp(1-(angleOfAttack-.32f)*2,.15f,1);
    float drag=v.speed*(.018f+.0017f*v.speed)+.00065f*v.speed*v.speed*liftCoefficient*liftCoefficient;
    if(onGround)drag+=.55f;
    v.speed=clamp(v.speed+(v.throttle*(v.health>25?11.5f:6.0f)-drag-9.81f*std::sin(v.pitch))*dt,0,100);
    if(onGround&&input.brake)v.speed=towards(v.speed,0,dt*9);
    const Vec3 horizontal=forward(v.yaw)*v.speed;const float response=std::min(1.0f,dt*(onGround?6.0f:2.0f));
    v.velocity.x=lerp(v.velocity.x,horizontal.x,response);v.velocity.z=lerp(v.velocity.z,horizontal.z,response);
    const float lift=.0105f*v.speed*v.speed*liftCoefficient*std::cos(v.roll);
    v.velocity.y=clamp(v.velocity.y+(lift-9.81f-v.velocity.y*.18f)*dt,-48,32);
    if(onGround&&(v.speed<24||v.pitch<.035f||lift<9.81f))v.velocity.y=0;
    if(v.position.y>1150)v.velocity.y=std::min(v.velocity.y,-3.0f);
    const Vec3 before=v.position,delta=v.velocity*dt;Vec3 candidate=before+delta;float impact=0;
    const float distance=length(delta);
    if(distance>.001f){
        float travel=distance;const Vec3 direction=delta/distance;
        for(Vec3 local:std::array<Vec3,5>{{{0,1.3f,0},{-4.4f,1.45f,0},{4.4f,1.45f,0},{0,1.2f,3.0f},{0,1.3f,-2.8f}}}){
            Vec3 origin=before+rotate(local,v.yaw);
            for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids){Box expanded{box.min-Vec3{.28f,.28f,.28f},box.max+Vec3{.28f,.28f,.28f}};travel=std::min(travel,rayBox(origin,direction,expanded,travel));}
        }
        if(travel<distance){impact=std::max(5.0f,v.speed*.9f+std::abs(v.velocity.y)*3);candidate=before+direction*std::max(0.0f,travel-.03f);v.speed*=.12f;v.velocity=v.velocity*-.04f;v.velocity.y=std::min(0.0f,v.velocity.y);}
    }
    const float limit=World::Extent-2;
    if(std::abs(candidate.x)>limit||std::abs(candidate.z)>limit){
        candidate.x=clamp(candidate.x,-limit,limit);candidate.z=clamp(candidate.z,-limit,limit);
        v.speed=0;v.velocity.x=0;v.velocity.z=0;v.velocity.y=std::min(0.0f,v.velocity.y);
    }
    ground=world.height(candidate.x,candidate.z);const bool water=world.waterDepth(candidate.x,candidate.z)>.5f;
    if(water)ground=std::max(ground,World::WaterLevel);
    if(candidate.y<ground){
        if(water){impact=std::max(impact,100.0f);v.speed*=.35f;v.throttle=0;}
        else impact=std::max(impact,std::max(0.0f,-v.velocity.y-3.5f)*12+std::max(0.0f,std::abs(v.roll)-.35f)*80+std::max(0.0f,-v.pitch-.14f)*100);
        candidate.y=ground;v.velocity.y=0;v.pitch=lerp(v.pitch,0,dt*3);v.roll=lerp(v.roll,0,dt*5);
    }
    v.health=std::max(0.0f,v.health-impact);v.position=candidate;
    return impact;
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
        {"SIGNAL ON THE WATER","Mara: Our witness left two recordings along the eastern promenade. Find them on foot; let the city hear the truth.",{12,0,-512},{780,0,-512},1600},
        {"A LIGHT IN THE SOUND","Mara: Leena's clinic launch lost power. Take the blue loan boat, hold alongside her red beacon, then bring her and the medical supplies back to Glasswater Landing.",LandingPier,RescuePickup,1800},
        {"LINES ABOVE THE COAST","Leena: My radio log points to three unlisted harbor transmitters. Fly the survey gates in the cream loan plane, then land with the recorder at Breaker Airfield.",AirfieldStart,SurveyPoints[0],2600}
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
    appendStarterCraft(vehicles,world);
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
const char* Game::missionInstruction() const {
    if(activeMission<0)return missionInfo()?"MEET THE CONTACT AT THE AMBER MARKER":"EXPLORE MERIDIAN COAST";
    switch(activeMission){
    case 0:return missionStage==0?"ENTER A CAR OR MOTORCYCLE":"DELIVER THE VEHICLE TO THE HARBOR STEPS";
    case 1:return missionStage==0?"COLLECT THE CLINIC BATTERIES AT THE ARCADE":"DELIVER THE BATTERIES TO WESTHAVEN";
    case 2:return missionStage==0?"ENTER A CAR OR MOTORCYCLE":"LOSE THE PATROL; RETURN TO MARA";
    case 3:return missionStage==0?"FIND THE FIRST RECORDING ON FOOT":"FIND THE PROMENADE RECORDING ON FOOT";
    case 4:
        if(missionStage==0)return "BOARD THE BLUE LOAN BOAT AT THE PIER";
        if(missionStage>=2)return "RETURN LEENA TO GLASSWATER; STOP AT PIER";
        return missionHold>0?"HOLD BESIDE LEENA'S LAUNCH FOR 3 SECONDS":"REACH THE RED BEACON; SLOW BESIDE LEENA";
    case 5:
        if(missionStage==0)return "FLY GATE 1 OF 3 / 50-130 M ABOVE TERRAIN";
        if(missionStage==1)return "FLY GATE 2 OF 3 / 90-190 M ABOVE TERRAIN";
        if(missionStage==2)return "FLY GATE 3 OF 3 / 60-160 M ABOVE TERRAIN";
        return "LAND AND STOP ON THE BREAKER RUNWAY";
    default:return "FOLLOW THE CONTRACT MARKER";
    }
}
Vec3 Game::missionTarget() const {
    const Mission* mission=missionInfo();if(!mission)return World::garageSite().marker;
    if(activeMission<0)return mission->start;
    if(activeMission==1&&missionStage>=1)return DispatchDestination;
    if(activeMission==3&&missionStage>=1)return SignalDestination;
    if(activeMission==4)return missionStage>=2?LandingPier:RescuePickup;
    if(activeMission==5){if(missionStage>=3)return AirfieldStop;Vec3 point=SurveyPoints[std::max(0,missionStage)];point.y+=world.height(point.x,point.z);return point;}
    return mission->target;
}
Vec3 Game::harborSplitContact(){return SplitContact;}
Vec3 Game::harborSplitStart(){return SplitStart;}
float Game::harborSplitLimit(){return SplitLimit;}
const std::vector<Vec3>& Game::harborSplitCourse(){
    static const std::vector<Vec3> gates{{259.2f,0,-131.2f},{508.8f,0,-131.2f},{508.8f,0,-380.8f},
        {252.8f,0,-380.8f},{252.8f,0,-508.8f},{131.2f,0,-508.8f},{131.2f,0,-259.2f},
        {259.2f,0,-259.2f},SplitStart};
    return gates;
}
const Mission* Game::objectiveInfo() const {
    return objectiveIsTrial()?&splitDescription():missionInfo();
}
bool Game::objectiveIsTrial() const {
    return harborSplit.phase!=TrialPhase::Inactive||
        (activeMission<0&&(planarDistance(player,SplitContact)<50||!missionInfo()));
}
Vec3 Game::objectiveTarget() const {
    if(harborSplit.phase==TrialPhase::Boarding&&splitVehicle>=0&&size_t(splitVehicle)<vehicles.size())
        return occupied==splitVehicle?atGround(world,SplitStart):vehicles[size_t(splitVehicle)].position;
    if(harborSplit.phase==TrialPhase::Countdown)return atGround(world,SplitStart);
    if(harborSplit.phase==TrialPhase::Running)return atGround(world,harborSplitCourse()[size_t(harborSplit.checkpoint)]);
    return objectiveIsTrial()?atGround(world,SplitContact):missionTarget();
}
bool Game::objectiveActive() const {return activeMission>=0||harborSplit.phase!=TrialPhase::Inactive;}
float Game::objectiveTimeRemaining() const {
    if(harborSplit.phase==TrialPhase::Running)return std::max(0.0f,SplitLimit-harborSplit.elapsed-harborSplit.penalty);
    if(harborSplit.phase!=TrialPhase::Inactive)return std::max(0.0f,harborSplit.countdown);
    return activeMission>=0?missionTimer:0;
}
const char* Game::objectiveInstruction() const {
    char line[160];
    if(harborSplit.phase==TrialPhase::Boarding)return "BOARD YOUR BIKE; STOP AT THE START LINE";
    if(harborSplit.phase==TrialPhase::Countdown){std::snprintf(line,sizeof(line),"HOLD POSITION / START IN %d",int(std::ceil(harborSplit.countdown)));objectiveDescription=line;return objectiveDescription.c_str();}
    if(harborSplit.phase==TrialPhase::Running){
        std::snprintf(line,sizeof(line),"GATE %d / %d  /  %.1f S  /  PENALTY +%.0f S",harborSplit.checkpoint+1,int(harborSplitCourse().size()),harborSplit.elapsed,harborSplit.penalty);
        objectiveDescription=line;return objectiveDescription.c_str();
    }
    if(objectiveIsTrial()){
        if(harborSplit.bestTime>0){std::snprintf(line,sizeof(line),"M TO RIDE / BEST %.1f S / %s",harborSplit.bestTime,harborSplit.medal==3?"GOLD":harborSplit.medal==2?"SILVER":"BRONZE");objectiveDescription=line;return objectiveDescription.c_str();}
        return "M TO RIDE / GOLD 85 / SILVER 110 / BRONZE 150 S";
    }
    return missionInstruction();
}
const char* Game::workshopInstruction() const {
    const GarageSite site=World::garageSite();
    if(planarDistance(player,site.marker)>48||std::abs(player.y-site.floorHeight)>12)return nullptr;
    if(wanted>0)return "LOSE THE PATROL BEFORE REQUESTING WORKSHOP SERVICE";
    if(occupied>=0&&size_t(occupied)<vehicles.size()){
        const Vehicle& vehicle=vehicles[size_t(occupied)];
        if(!roadVehicle(vehicle.kind))return "CAR / MOTORCYCLE REPAIRS ONLY; USE THE FRONT BAY";
        if(!insideServiceArea(vehicle.position,site.serviceBay,site.floorHeight))return "DRIVE INTO THE OPEN BAY AND STOP ON THE SERVICE PAD";
        if(std::abs(vehicle.speed)>.5f||length(vehicle.velocity)>.6f)return "STOP COMPLETELY FOR BAY SERVICE";
        if(vehicle.health>=99.99f)return "VEHICLE READY; NO REPAIR NEEDED";
        return money>=75?"M / DPAD UP: REPAIR VEHICLE $75":"VEHICLE REPAIR $75; INSUFFICIENT CASH";
    }
    if(!insideServiceArea(player,site.customerArea,site.floorHeight))return "WALK THROUGH THE OFFICE DOOR TO THE FIRST-AID COUNTER";
    if(health>=99.99f)return "FIRST AID NOT NEEDED; VEHICLE REPAIRS IN THE BAY";
    return money>=25?"M / DPAD UP: FIRST AID $25":"FIRST AID $25; INSUFFICIENT CASH";
}
void Game::endHarborSplit(const char* reason){
    const float best=harborSplit.bestTime;const int medal=harborSplit.medal;
    harborSplit={};harborSplit.bestTime=best;harborSplit.medal=medal;splitVehicle=-1;splitDamageCooldown=0;
    message=std::string("HARBOR SPLIT / ")+reason+" Return to Rafi's violet flag and press M to ride again.";messageTime=12;
}
void Game::updateHarborSplit(float dt,float damage){
    if(harborSplit.phase==TrialPhase::Inactive)return;
    if(health<=0||player.y< -25){endHarborSplit("Run ended: rider injured.");return;}
    if(splitVehicle<0||size_t(splitVehicle)>=vehicles.size()||vehicles[size_t(splitVehicle)].kind!=VehicleKind::Motorcycle||vehicles[size_t(splitVehicle)].health<=0){endHarborSplit("Run ended: motorcycle lost.");return;}
    const Vehicle& bike=vehicles[size_t(splitVehicle)];
    if(harborSplit.phase==TrialPhase::Boarding){
        harborSplit.countdown=std::max(0.0f,harborSplit.countdown-dt);
        if(occupied==splitVehicle&&planarDistance(player,SplitStart)<9&&std::abs(bike.speed)<.5f){
            harborSplit.phase=TrialPhase::Countdown;harborSplit.countdown=3;
            message="Rafi: Three seconds. Hold steady, then follow the green gate. Violet previews the next turn. M withdraws.";messageTime=5;
        }else if(harborSplit.countdown==0)endHarborSplit("Run ended: start window expired.");
        return;
    }
    if(occupied!=splitVehicle){endHarborSplit("Run ended: stay on the same motorcycle.");return;}
    if(harborSplit.phase==TrialPhase::Countdown){
        if(planarDistance(player,SplitStart)>12){endHarborSplit("Run ended: start line obstructed.");return;}
        harborSplit.countdown=std::max(0.0f,harborSplit.countdown-dt);
        if(harborSplit.countdown==0){harborSplit.phase=TrialPhase::Running;message="GO! Nine gates. A clean line beats a reckless shortcut.";messageTime=5;}
        return;
    }
    harborSplit.elapsed+=dt;splitDamageCooldown=std::max(0.0f,splitDamageCooldown-dt);
    if(damage>.15f&&splitDamageCooldown==0){harborSplit.penalty+=5;splitDamageCooldown=1;message="Rafi: Contact! Five seconds added. Keep the next corner clean.";messageTime=4;}
    const float score=harborSplit.elapsed+harborSplit.penalty;
    if(score>=SplitLimit){endHarborSplit("Run ended: time limit reached.");return;}
    const Vec3 gate=harborSplitCourse()[size_t(harborSplit.checkpoint)];
    if(planarDistance(player,gate)>=6||std::abs(player.y-world.height(gate.x,gate.z))>2)return;
    ++harborSplit.checkpoint;
    if(harborSplit.checkpoint<int(harborSplitCourse().size()))return;
    const int earned=splitMedal(score),newMedal=std::max(harborSplit.medal,earned);
    const int reward=SplitPayout[newMedal]-SplitPayout[harborSplit.medal];
    harborSplit.medal=newMedal;harborSplit.bestTime=harborSplit.bestTime==0?score:std::min(harborSplit.bestTime,score);
    money=std::min(100000000,money+reward);
    char result[180];std::snprintf(result,sizeof(result),"%s / %.1f S including %.0f S penalties. %s",earned==3?"GOLD":earned==2?"SILVER":"BRONZE",score,harborSplit.penalty,reward>0?"New medal prize":"Practice finish; medal prize already earned");
    std::string finish=result;if(reward>0)finish+=" +$"+std::to_string(reward)+".";else finish+=".";
    endHarborSplit(finish.c_str());
}
Vec3 Game::cameraEye() const {
    const Vec3 anchor=player+Vec3{0,occupied>=0?1.65f:1.45f,0};
    const Vec3 direction=forward(yaw)*std::cos(pitch)+Vec3{0,-std::sin(pitch),0};
    float distance=occupied>=0?8.0f:(aiming?3.0f:5.5f);
    if(occupied>=0&&size_t(occupied)<vehicles.size()){if(vehicles[size_t(occupied)].kind==VehicleKind::Aircraft)distance=15;else if(vehicles[size_t(occupied)].kind==VehicleKind::Boat)distance=10;}
    Vec3 desired=anchor-direction*distance+right(yaw)*(aiming?.67f:.32f);
    desired.y=std::max(desired.y,world.height(desired.x,desired.z)+.35f);
    const auto clipCamera=[&](Vec3 point){
        const Vec3 delta=point-anchor,ray=normalized(delta);float maximum=length(delta);
        for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids){Box expanded{box.min-Vec3{.20f,.20f,.20f},box.max+Vec3{.20f,.20f,.20f}};maximum=std::min(maximum,rayBox(anchor,ray,expanded,maximum));}
        return anchor+ray*std::max(0.0f,maximum-.15f);
    };
    Vec3 result=clipCamera(desired);
    const float floor=world.height(result.x,result.z)+.35f;
    if(result.y<floor)result=clipCamera({result.x,floor,result.z});
    return result;
}
Vec3 Game::cameraTarget() const {
    Vec3 direction=forward(yaw)*std::cos(pitch)+Vec3{0,-std::sin(pitch),0};
    return player+Vec3{0,occupied>=0?1.65f:1.45f,0}+direction*35;
}
void Game::update(const Input& input,float elapsed,bool streamWorld){
    if(paused||!finite(elapsed)||elapsed<=0)return;
    const float dt=std::min(elapsed,.05f);
    Input in=input;
    in.moveX=finite(in.moveX)?clamp(in.moveX,-1,1):0;in.moveY=finite(in.moveY)?clamp(in.moveY,-1,1):0;
    in.lookX=finite(in.lookX)?clamp(in.lookX,-1,1):0;in.lookY=finite(in.lookY)?clamp(in.lookY,-1,1):0;
    const float splitHealthBefore=splitVehicle>=0&&size_t(splitVehicle)<vehicles.size()?vehicles[size_t(splitVehicle)].health:100.0f;
    if(harborSplit.phase==TrialPhase::Countdown){in.moveX=0;in.moveY=0;in.brake=true;}
    const bool interact=in.interact&&!wasInteract,reload=in.reload&&!wasReload,missionAction=in.mission&&!wasMission,radio=in.radio&&!wasRadio,jump=in.jump&&!wasJump;
    wasInteract=in.interact;wasReload=in.reload;wasMission=in.mission;wasRadio=in.radio;wasJump=in.jump;
    ++simulationTick;time+=dt;dayTime=std::fmod(dayTime+dt/75.0f,24.0f);
    rain=towards(rain,clamp((std::sin(time*.004f-1.5f)+.10f)*1.25f,0,1),dt*.018f);
    fireCooldown=std::max(0.0f,fireCooldown-dt);shotFlash=std::max(0.0f,shotFlash-dt);invulnerabilityTimer=std::max(0.0f,invulnerabilityTimer-dt);messageTime=std::max(0.0f,messageTime-dt);
    yaw=wrapAngle(yaw+in.lookX);pitch=clamp(pitch+in.lookY,-.85f,1.12f);aiming=in.aim&&occupied<0;
    if(std::abs(in.lookX)+std::abs(in.lookY)>.001f)cameraFollowDelay=2.4f;else cameraFollowDelay=std::max(0.0f,cameraFollowDelay-dt);
    if(occupied>=int(vehicles.size())||occupied< -1)occupied=-1;
    if(streamWorld)world.stream(player);
    if(trafficTargets.size()!=vehicles.size()){trafficTargets.clear();for(size_t i=0;i<vehicles.size();++i)trafficTargets.push_back(nextTrafficTarget(world,vehicles[i],uint32_t(i)));}
    if(pedestrianTargets.size()!=pedestrians.size()){pedestrianTargets.clear();for(size_t i=0;i<pedestrians.size();++i)pedestrianTargets.push_back(pedestrianCorner(world,pedestrians[i].position,uint32_t(i)));}
    if(radio){radioStation=(radioStation+1)%4;static const char* names[]={"Radio off","TIDELINE FM","NIGHT WINDOW","ION DRIVE"};message=names[radioStation];messageTime=4;}
    if(interact){
        if(occupied>=0){
            Vehicle& v=vehicles[size_t(occupied)];
            const float landingHeight=std::max(world.height(v.position.x,v.position.z),world.waterDepth(v.position.x,v.position.z)>.4f?World::WaterLevel: -100.0f);
            if(v.kind==VehicleKind::Aircraft&&v.position.y>landingHeight+.7f){message="Land the aircraft and slow to a taxi before exiting.";messageTime=4;}
            else if(std::abs(v.speed)>(v.kind==VehicleKind::Boat?3.0f:8.0f)){message=v.kind==VehicleKind::Boat?"Bring the boat below 11 km/h before leaving the helm.":"Slow below 30 km/h before getting out.";messageTime=3;}
            else {
                bool found=false,hasWaterExit=false,swimmingExit=false;Vec3 waterExit;
                const float scale=roadVehicle(v.kind)?1.0f:1.9f;
                const auto safeExit=[&](Vec3 exit){
                    if(world.blocked(exit,.35f)||!passengerPathClear(world,v.position,exit))return false;
                    for(size_t i=0;i<vehicles.size();++i){
                        if(int(i)==occupied||std::abs(vehicles[i].position.y-exit.y)>2.5f)continue;
                        const float clearance=roadVehicle(vehicles[i].kind)?1.15f:2.1f;
                        if(planarDistance(exit,vehicles[i].position)<clearance)return false;
                    }
                    return true;
                };
                for(Vec3 offset:std::array<Vec3,6>{{{-2.0f,0,0},{2.0f,0,0},{0,0,-3.0f},{0,0,3.0f},{-3.0f,0,0},{3.0f,0,0}}}){
                    Vec3 exit=atGround(world,v.position+rotate(offset*scale,v.yaw));
                    if(v.kind==VehicleKind::Boat&&exit.y<World::WaterLevel+.05f)continue;
                    if(v.kind==VehicleKind::Aircraft&&world.waterDepth(exit.x,exit.z)>.9f&&exit.y<World::WaterLevel){
                        exit.y=World::WaterLevel-1.1f;
                        if(!hasWaterExit&&safeExit(exit)){waterExit=exit;hasWaterExit=true;}
                        continue;
                    }
                    if(safeExit(exit)){player=exit;found=true;break;}
                }
                if(!found&&hasWaterExit){player=waterExit;found=true;swimmingExit=true;}
                if(!found&&v.kind==VehicleKind::Boat)for(Vec3 offset:std::array<Vec3,4>{{{2,0,0},{-2,0,0},{0,0,-4},{0,0,4}}}){
                    Vec3 exit=v.position+rotate(offset,v.yaw);const float depth=world.waterDepth(exit.x,exit.z);
                    exit.y=std::max(world.height(exit.x,exit.z),World::WaterLevel-1.1f);
                    if(depth>.05f&&safeExit(exit)){player=exit;found=true;swimmingExit=depth>.9f&&exit.y<World::WaterLevel+.25f;break;}
                }
                if(found){v.parked=true;v.speed=0;v.velocity={};v.throttle=0;occupied=-1;verticalSpeed=0;grounded=!swimmingExit;message=swimmingExit?"In the water. Swim with WASD; hold Shift for a faster stroke.":v.kind==VehicleKind::Boat?"On the landing. E boards a nearby boat.":"On foot. Hold right mouse to aim; left mouse fires.";messageTime=5;}
                else {message="Both doors are obstructed. Move the vehicle to open ground.";messageTime=3;}
            }
        }else{
            float nearest=6.0f;int index=-1;
            for(size_t i=0;i<vehicles.size();++i)if(vehicles[i].health>0&&std::abs(vehicles[i].speed)<6&&std::abs(player.y-vehicles[i].position.y)<3.2f){float distance=planarDistance(player,vehicles[i].position);if(distance<nearest&&passengerPathClear(world,player,vehicles[i].position)){nearest=distance;index=int(i);}}
            if(index>=0){occupied=index;Vehicle& v=vehicles[size_t(index)];v.parked=false;player=v.position;verticalSpeed=0;grounded=true;reloadTimer=0;cameraFollowDelay=0;yaw=v.yaw;
                if(v.police){wanted=std::max(wanted,2);wantedTimer=28;message="Patrol vehicle taken. Dispatch has your description.";}
                else if(v.kind==VehicleKind::Boat)message="RUNABOUT  /  W/S thrust and reverse  A/D rudder  Space slow  E leave helm";
                else if(v.kind==VehicleKind::Aircraft)message="SKYLARK  /  W/S set throttle  A/D bank  Shift climb  Space descend/brake  E exit after landing";
                else message=v.kind==VehicleKind::Motorcycle?"Motorcycle  /  W/S throttle and reverse  A/D steer  Space brake":"W/S throttle and reverse  A/D steer  Space handbrake  E exit  Q radio";
                messageTime=5;
            }else {message="Move beside a stopped vehicle and press E.";messageTime=3;}
        }
    }
    if(occupied<0){
        Vec3 move=forward(yaw)*in.moveY+right(yaw)*in.moveX;float amount=std::min(1.0f,length(move));move=normalized(move)*amount;
        const bool swimming=world.waterDepth(player.x,player.z)>.9f&&player.y<World::WaterLevel+.25f;
        float speed=swimming?(in.sprint?3.4f:2.2f):(aiming?2.2f:(in.sprint?7.3f:3.9f));
        Vec3 delta=move*(speed*dt);Vec3 before=player;player=world.move(player,delta,.34f);
        const float ground=world.height(player.x,player.z);
        if(swimming){player.y=std::max(ground,lerp(player.y,World::WaterLevel-1.1f,dt*5));verticalSpeed=0;grounded=false;aiming=false;}
        else {if(jump&&grounded){verticalSpeed=5.8f;grounded=false;}verticalSpeed-=18*dt;player.y+=verticalSpeed*dt;if(player.y<=ground){player.y=ground;verticalSpeed=0;grounded=true;}}
        playerMotion=towards(playerMotion,amount,dt*7);playerPhase+=planarDistance(player,before)*2.5f;
    }else {
        Vehicle& v=vehicles[size_t(occupied)];
        if(v.kind==VehicleKind::Boat){float impact=simulateBoat(v,world,in,dt,time,rain);health=std::max(0.0f,health-impact*.12f);}
        else if(v.kind==VehicleKind::Aircraft){float impact=simulateAircraft(v,world,in,dt);health=std::max(0.0f,health-std::max(0.0f,impact-8)*.8f);}
        else {
            const bool bike=v.kind==VehicleKind::Motorcycle;
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
        }
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
        if(v.kind==VehicleKind::Boat){Input idle;simulateBoat(v,world,idle,dt,time,rain);continue;}
        if(v.kind==VehicleKind::Aircraft){
            v.throttle=0;
            const float surface=std::max(world.height(v.position.x,v.position.z),world.waterDepth(v.position.x,v.position.z)>.5f?World::WaterLevel:-100.0f);
            const bool landed=v.position.y<=surface+.02f&&std::abs(v.velocity.y)<.3f;
            if(landed&&std::abs(v.speed)<.10f&&length(v.velocity)<.20f){
                v.speed=0;v.velocity={};v.position.y=surface;v.pitch=0;v.roll=0;v.parked=true;
            }else {Input idle;idle.brake=landed;v.parked=false;simulateAircraft(v,world,idle,dt);}
            continue;
        }
        if(v.health<=0){v.speed=towards(v.speed,0,dt*8);continue;}
        float targetSpeed=0;
        Vec3 target=trafficTargets[i];
        if(v.police&&wanted>0){
            v.parked=false;float distance=planarDistance(v.position,player);bool hasSight=false;
            const Vec3 eye=v.position+Vec3{0,1,0},toPlayer=player+Vec3{0,1,0}-eye;
            const float sightDistance=length(toPlayer);
            if(sightDistance<95){
                const Vec3 ray=normalized(toPlayer);float visible=sightDistance;
                for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids)visible=std::min(visible,rayBox(eye,ray,box,visible));
                if(visible>=sightDistance-.1f){policeSight=true;hasSight=true;}
            }
            if(distance<28)target=player;
            else {float grid=roadGrid(v.position);Vec3 node{std::round(v.position.x/grid)*grid,0,std::round(v.position.z/grid)*grid};Vec3 delta=player-node;if(std::abs(delta.x)>std::abs(delta.z))target=node+Vec3{delta.x>0?grid:-grid,0,Lane};else target=node+Vec3{Lane,0,delta.z>0?grid:-grid};}
            targetSpeed=distance<8?3.0f:22+float(wanted)*2;
            if(hasSight&&length(v.position-player)<6&&occupied<0&&invulnerabilityTimer<=0){health=std::max(0.0f,health-dt*(7+float(wanted)*3));}
        }else if(v.parked){v.speed=0;v.velocity={};continue;}
        else {
            if(planarDistance(v.position,target)<11){trafficTargets[i]=nextTrafficTarget(world,v,uint32_t(i)*391+simulationTick);target=trafficTargets[i];}
            targetSpeed=10+random01(uint32_t(i)*193)*7;
            if(wanted==0&&v.police)targetSpeed=9;
        }
        Vec3 direction=target-v.position;direction.y=0;float desiredYaw=std::atan2(direction.x,direction.z),turn=wrapAngle(desiredYaw-v.yaw);
        if(std::abs(turn)>.7f)targetSpeed=std::min(targetSpeed,6.0f);
        for(size_t j=0;j<vehicles.size();++j){if(i==j||std::abs(vehicles[j].position.y-v.position.y)>2.5f)continue;Vec3 offset=vehicles[j].position-v.position;float ahead=dot(offset,forward(v.yaw));if(ahead>0&&ahead<10+std::abs(v.speed)*.65f&&std::abs(dot(offset,right(v.yaw)))<2.3f)targetSpeed=std::min(targetSpeed,std::max(0.0f,(ahead-5)*.75f));}
        if(occupied<0){Vec3 offset=player-v.position;if(dot(offset,forward(v.yaw))>0&&planarDistance(player,v.position)<10&&std::abs(dot(offset,right(v.yaw)))<2)targetSpeed=v.police&&wanted>0?2.0f:0.0f;}
        v.speed=towards(v.speed,targetSpeed,dt*(targetSpeed<v.speed?8:3.8f));v.steer=clamp(turn*1.5f,-1,1);
        v.yaw=wrapAngle(v.yaw+clamp(turn,-1.2f*dt,1.2f*dt));v.velocity=forward(v.yaw)*v.speed;
        Vec3 before=v.position;v.position=world.move(v.position,v.velocity*dt,v.kind==VehicleKind::Motorcycle?.45f:1.0f);v.position=atGround(world,v.position);
        if(planarDistance(before,v.position)<.002f&&v.speed>1){v.speed=0;trafficTargets[i]=nextTrafficTarget(world,v,uint32_t(i)+simulationTick+827);}
    }
    if(occupied>=0){
        Vehicle& driven=vehicles[size_t(occupied)];
        for(size_t i=0;i<vehicles.size();++i){if(int(i)==occupied)continue;Vehicle& other=vehicles[i];if(std::abs(driven.position.y-other.position.y)>2.5f)continue;float radius=driven.kind==VehicleKind::Aircraft?4.5f:(driven.kind==VehicleKind::Boat?3.2f:(driven.kind==VehicleKind::Motorcycle?1.55f:2.15f));Vec3 delta=driven.position-other.position;delta.y=0;float distance=length(delta);
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
                const float distance=length(toPlayer);
                if(distance<85){
                    float visible=distance;
                    for(const Chunk& chunk:world.chunks)for(const Box& box:chunk.solids)visible=std::min(visible,rayBox(eye,normalized(toPlayer),box,visible));
                    if(visible>=distance-.1f){policeSight=true;if(distance<15){speed=0;if(invulnerabilityTimer<=0)health=std::max(0.0f,health-dt*(2.0f+float(wanted)));}}
                }
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
        for(size_t j=0;j<vehicles.size();++j){Vehicle& v=vehicles[j];if(std::abs(v.speed)<2.5f||std::abs(v.position.y-p.position.y)>2.5f)continue;float distanceToCar=planarDistance(p.position,v.position);if(distanceToCar<2){p.health=std::max(0.0f,p.health-std::abs(v.speed)*7);p.position=world.move(p.position,normalized(p.position-v.position)*2,.28f);p.panic=12;if(int(j)==occupied){wanted=std::max(wanted,i<4?3:2);wantedTimer=32;v.speed*=.84f;}}else if(distanceToCar<7&&i>=4)p.panic=std::max(p.panic,3.0f);}
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
            Vehicle& v=vehicles[i];if(!roadVehicle(v.kind)||int(i)==occupied||planarDistance(v.position,player)<430)continue;
            float grid=roadGrid(player);float angle=float(hash32(uint32_t(i)+simulationTick)%4u)*Pi*.5f;Vec3 node{std::round(player.x/grid)*grid,0,std::round(player.z/grid)*grid};Vec3 p=node+forward(angle)*grid*2+right(angle)*Lane;
            if(world.biome(p.x,p.z)==Biome::Ocean)continue;
            v.position=atGround(world,p);v.yaw=wrapAngle(angle+Pi);v.speed=v.police?0.0f:8.0f;v.health=100;v.parked=v.police&&wanted==0;trafficTargets[i]=nextTrafficTarget(world,v,uint32_t(i)+simulationTick);
        }
        for(size_t i=4;i<pedestrians.size();++i){Pedestrian& p=pedestrians[i];if(planarDistance(p.position,player)<330)continue;float grid=roadGrid(player);uint32_t h=hash32(uint32_t(i)+simulationTick);Vec3 base{std::floor(player.x/grid)*grid+float(int(h%5u)-2)*grid,0,std::floor(player.z/grid)*grid+float(int((h>>4)%5u)-2)*grid};base+=Vec3{12,0,12};if(world.biome(base.x,base.z)==Biome::Ocean)continue;p.position=atGround(world,base);p.health=100;p.panic=0;pedestrianTargets[i]=pedestrianCorner(world,p.position,h);}
    }
    if(missionAction){
        if(harborSplit.phase!=TrialPhase::Inactive)endHarborSplit("Run withdrawn.");
        else if(planarDistance(player,SplitContact)<14){
            if(activeMission>=0){message="Rafi: Finish your current contract before we time a run.";messageTime=6;}
            else if(wanted>0){message="Rafi: Lose the patrol first. We cannot start under pursuit.";messageTime=6;}
            else {
                splitVehicle=provideTrialBike(world,vehicles,occupied);
                if(splitVehicle<0){message="Rafi: The staging lane is blocked. Clear some space beside the violet flag.";messageTime=6;}
                else {
                    const float best=harborSplit.bestTime;const int medal=harborSplit.medal;
                    harborSplit={};harborSplit.bestTime=best;harborSplit.medal=medal;harborSplit.phase=TrialPhase::Boarding;harborSplit.countdown=60;splitDamageCooldown=0;
                    message=std::string(splitDescription().briefing)+" Board the violet loan bike, or use the motorcycle you brought. Stop at the start marker.";messageTime=14;
                }
            }
        }
        else if(activeMission<0&&completedMissions<int(missions().size())&&planarDistance(player,missions()[size_t(completedMissions)].start)<14){
            const int chapter=completedMissions;
            const bool supplied=chapter<4||provideLoan(world,vehicles,occupied,chapter==4?VehicleKind::Boat:VehicleKind::Aircraft);
            if(supplied){activeMission=chapter;missionStage=0;missionHold=0;missionTimer=chapter==1?180.0f:(chapter==2||chapter==4?240.0f:(chapter==5?360.0f:0.0f));message=missions()[size_t(chapter)].briefing;if(chapter>=4)message+=chapter==4?" A serviced blue loan runabout is ready at the berth.":" A serviced cream survey plane is ready on the runway.";messageTime=14;}
            else {message="The loan craft needs a clear berth. Move your current vehicle aside and contact the crew again.";messageTime=6;}
        }
        else if(const char* instruction=workshopInstruction()){
            const GarageSite site=World::garageSite();message=instruction;
            if(wanted==0&&occupied>=0){
                Vehicle& vehicle=vehicles[size_t(occupied)];
                if(roadVehicle(vehicle.kind)&&insideServiceArea(vehicle.position,site.serviceBay,site.floorHeight)&&std::abs(vehicle.speed)<=.5f&&length(vehicle.velocity)<=.6f&&vehicle.health<99.99f&&money>=75){
                    money-=75;vehicle.health=100;message="HARBOR MOTOR WORKS / Vehicle repaired. Parts and labor -$75.";
                }
            }else if(wanted==0&&occupied<0&&insideServiceArea(player,site.customerArea,site.floorHeight)&&health>0&&health<99.99f&&money>=25){
                money-=25;health=100;message="HARBOR MOTOR WORKS / First aid supplied. Treatment -$25.";
            }
            messageTime=6;
        }
        else if(planarDistance(player,Outfitter)<16){if(money>=60&&reserveAmmo<=9910){money-=60;reserveAmmo+=90;message="Outfitter  /  90 rounds  -$60";}else message="Outfitter  /  ammunition costs $60";messageTime=5;}
        else if(activeMission<0){message=completedMissions<int(missions().size())?"Meet your contact at the amber marker to begin the next job.":"The harbor story is complete. Explore the coast, ride the city, or visit the garage and outfitter.";messageTime=5;}
        else {message=missions()[size_t(activeMission)].briefing;messageTime=8;}
    }
    if(activeMission>=0){
        bool complete=false,failed=health<=0;const float distance=planarDistance(player,missionTarget());const float speed=occupied>=0?std::abs(vehicles[size_t(occupied)].speed):0;const bool roadRide=occupied>=0&&roadVehicle(vehicles[size_t(occupied)].kind);
        if(missionTimer>0){missionTimer=std::max(0.0f,missionTimer-dt);if(missionTimer==0)failed=true;}
        switch(activeMission){
        case 0:
            if(missionStage==0&&roadRide){missionStage=1;message="Inez: That's the one. Bring it to the harbor steps, east along the avenue.";messageTime=7;}
            if(missionStage==1&&roadRide&&distance<17&&speed<4)complete=true;
            break;
        case 1:
            if(missionStage==0&&distance<16&&roadRide&&speed<4){missionStage=1;missionTimer=130;message="Batteries loaded. Mara is waiting in Westhaven. Deliver before the clinic loses power.";messageTime=9;}
            else if(missionStage==1&&distance<16&&roadRide&&speed<4)complete=true;
            break;
        case 2:
            if(missionStage==0&&roadRide){missionStage=1;wanted=std::max(wanted,2);wantedTimer=22;message="Dispatch intercepted the call. Lose the patrols, then meet Mara south of the canal.";messageTime=9;}
            if(missionStage==1&&distance<24){missionStage=2;message=wanted>0?"Mara: Don't lead them here. Break line of sight, then circle back.":"Mara: You're clear. Bring the records to me.";messageTime=8;}
            if(missionStage==2&&wanted==0&&distance<30)complete=true;
            break;
        case 3:
            if(missionStage==0&&distance<10&&occupied<0){missionStage=1;message="Recording 1: The harbor fees paid for private patrols. The second recording is north on the promenade.";messageTime=10;}
            else if(missionStage==1&&distance<10&&occupied<0)complete=true;
            break;
        case 4: {
            const bool matching=occupied>=0&&vehicles[size_t(occupied)].kind==VehicleKind::Boat;
            const bool boat=matching&&vehicles[size_t(occupied)].health>0;
            if(matching&&!boat)failed=true;
            if(missionStage==0&&boat){missionStage=1;message="Mara: Find the red beacon east of the landing. Slow alongside Leena's clinic launch for the transfer.";messageTime=9;}
            if(missionStage==1){
                if(boat&&distance<14&&speed<2){if(missionHold==0){message="Leena: Hold steady alongside. I need three seconds to cross with the supplies.";messageTime=5;}missionHold+=dt;
                    if(missionHold>=3){missionHold=0;missionStage=2;message="Leena: I'm aboard, and the supplies are secure. Take us back to Glasswater Landing.";messageTime=10;}}
                else missionHold=0;
            }else if(missionStage==2&&boat&&distance<16&&speed<2)complete=true;
            break;
        }
        case 5: {
            const bool matching=occupied>=0&&vehicles[size_t(occupied)].kind==VehicleKind::Aircraft;
            const bool aircraft=matching&&vehicles[size_t(occupied)].health>0;
            if(matching&&!aircraft)failed=true;
            const float altitude=player.y-world.height(player.x,player.z);
            if(missionStage<3&&aircraft&&distance<75&&speed>24&&altitude>=SurveyMinimum[missionStage]&&altitude<=SurveyMaximum[missionStage]){
                ++missionStage;
                if(missionStage==1)message="Recorder: First transmitter logged. Next gate east; hold 90 to 190 metres above the terrain.";
                else if(missionStage==2)message="Recorder: Second transmitter logged. Final gate north; hold 60 to 160 metres above the terrain.";
                else message="Leena: All three transmitters are recorded. Land at Breaker Airfield and bring the plane to a stop.";
                messageTime=10;
            }else if(missionStage==3&&aircraft&&std::abs(player.x-AirfieldStop.x)<14&&player.z>-1220&&player.z<-780&&std::abs(altitude)<.35f&&speed<4.5f&&std::abs(vehicles[size_t(occupied)].velocity.y)<1)complete=true;
            break;
        }
        default:failed=true;break;
        }
        if(complete&&!failed){const Mission& m=missions()[size_t(activeMission)];money=std::min(100000000,money+m.reward);message=std::string("JOB COMPLETE  /  ")+m.title+"  +$"+std::to_string(m.reward);if(activeMission==3)message+="  /  Mara: The recordings are on the air. Meet the clinic crew at Glasswater Landing.";messageTime=12;++completedMissions;activeMission=-1;missionStage=0;missionTimer=0;missionHold=0;health=std::min(100.0f,health+20);}
        else if(failed){message=activeMission>=4?"Contract interrupted. Return to the contact; the crew will service a replacement loan craft.":"Job expired. Return to your contact to try again.";messageTime=7;activeMission=-1;missionStage=0;missionTimer=0;missionHold=0;}
    }
    const float splitDamage=splitVehicle>=0&&size_t(splitVehicle)<vehicles.size()?std::max(0.0f,splitHealthBefore-vehicles[size_t(splitVehicle)].health):0;
    updateHarborSplit(dt,splitDamage);
    if(health<=0||player.y< -25){
        health=100;money=std::max(0,money-100);wanted=0;wantedTimer=0;occupied=-1;activeMission=-1;missionStage=0;missionTimer=0;missionHold=0;player=atGround(world,{12,0,12});yaw=0;pitch=.2f;verticalSpeed=0;grounded=true;invulnerabilityTimer=5;
        message="Recovered at Harbor Clinic. Treatment -$100. Your completed jobs and possessions are safe.";messageTime=9;
    }else if(wanted==0&&health<35)health=std::min(35.0f,health+dt*1.5f);
    if(streamWorld)world.stream(player);
}
bool Game::save(const std::string& path) const {
    try {
        if(path.empty()||vehicles.size()>256||pedestrians.size()>512)return false;
        SaveWriter payload;
        payload.vector(player);payload.real(yaw);payload.real(pitch);payload.real(health);payload.integer(money);payload.integer(ammo);payload.integer(reserveAmmo);payload.integer(wanted);
        payload.integer(occupied);payload.integer(activeMission);payload.integer(missionStage);payload.integer(completedMissions);payload.integer(radioStation);
        payload.real(time);payload.real(dayTime);payload.real(rain);payload.real(missionTimer);payload.real(wantedTimer);payload.real(reloadTimer);payload.real(verticalSpeed);payload.u32(grounded?1u:0u);
        payload.u32(uint32_t(vehicles.size()));
        for(const Vehicle& v:vehicles){payload.vector(v.position);payload.real(v.yaw);payload.real(v.speed);payload.real(v.steer);payload.vector(v.velocity);payload.vector(v.color);payload.integer(int(v.kind));payload.u32(v.police?1u:0u);payload.u32(v.parked?1u:0u);payload.real(v.health);payload.real(v.pitch);payload.real(v.roll);payload.real(v.throttle);}
        payload.u32(uint32_t(pedestrians.size()));
        for(const Pedestrian& p:pedestrians){payload.vector(p.position);payload.real(p.yaw);payload.real(p.phase);payload.real(p.panic);payload.real(p.health);}
        payload.real(missionHold);
        payload.real(harborSplit.bestTime);payload.integer(harborSplit.medal);payload.u32(harborSplit.phase!=TrialPhase::Inactive?1u:0u);
        SaveWriter header;for(char c:std::string("MCSTSAVE"))header.data.push_back(uint8_t(c));header.u32(4);header.u32(uint32_t(payload.data.size()));header.u32(crc32(payload.data.data(),payload.data.size()));
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
        SaveReader header{bytes,8};const uint32_t version=header.u32();if(version<1||version>4)return false;uint32_t payloadSize=header.u32(),checksum=header.u32();if(payloadSize!=bytes.size()-20)return false;if(crc32(bytes.data()+20,payloadSize)!=checksum)return false;
        Game state;SaveReader reader{bytes,20};
        state.player=reader.vector();state.yaw=reader.real();state.pitch=reader.real();state.health=reader.real();state.money=reader.integer();state.ammo=reader.integer();state.reserveAmmo=reader.integer();state.wanted=reader.integer();
        state.occupied=reader.integer();state.activeMission=reader.integer();state.missionStage=reader.integer();state.completedMissions=reader.integer();state.radioStation=reader.integer();
        state.time=reader.real();state.dayTime=reader.real();state.rain=reader.real();state.missionTimer=reader.real();state.wantedTimer=reader.real();state.reloadTimer=reader.real();state.verticalSpeed=reader.real();uint32_t ground=reader.u32();state.grounded=ground==1;
        if(!reader.good||!validPosition(state.player)||std::abs(state.yaw)>Pi*2||state.pitch<-.85f||state.pitch>1.12f||state.health<0||state.health>100||state.money<0||state.money>100000000||state.ammo<0||state.ammo>Magazine||state.reserveAmmo<0||state.reserveAmmo>10000||state.wanted<0||state.wanted>5||state.radioStation<0||state.radioStation>3)return false;
        if(state.completedMissions<0||state.completedMissions>int(missions().size())||state.activeMission< -1||state.activeMission>=int(missions().size())||state.missionStage<0||state.missionStage>3||(state.activeMission>=0&&state.activeMission!=state.completedMissions))return false;
        if((state.activeMission<0&&state.missionStage!=0)||(state.activeMission!=2&&state.activeMission!=4&&state.activeMission!=5&&state.missionStage>1)||((state.activeMission==2||state.activeMission==4)&&state.missionStage>2)||state.time<0||state.time>1e9f||state.dayTime<0||state.dayTime>=24||state.rain<0||state.rain>1||state.missionTimer<0||state.missionTimer>10000||state.wantedTimer<0||state.wantedTimer>10000||state.reloadTimer<0||state.reloadTimer>2||std::abs(state.verticalSpeed)>100||ground>1)return false;
        uint32_t vehicleCount=reader.u32();if(vehicleCount>256)return false;state.vehicles.reserve(vehicleCount);
        for(uint32_t i=0;i<vehicleCount;++i){Vehicle v;v.position=reader.vector();v.yaw=reader.real();v.speed=reader.real();v.steer=reader.real();v.velocity=reader.vector();v.color=reader.vector();int kind=reader.integer();uint32_t police=reader.u32(),parked=reader.u32();v.police=police==1;v.parked=parked==1;v.kind=VehicleKind(kind);v.health=reader.real();
            if(version>=2){v.pitch=reader.real();v.roll=reader.real();v.throttle=reader.real();}
            if(!reader.good||!validPosition(v.position)||std::abs(v.yaw)>Pi*2||std::abs(v.speed)>150||std::abs(v.steer)>1.01f||length(v.velocity)>200||v.color.x<0||v.color.x>1||v.color.y<0||v.color.y>1||v.color.z<0||v.color.z>1||kind<0||kind>3||police>1||parked>1||v.health<0||v.health>100||std::abs(v.pitch)>Pi*.5f||std::abs(v.roll)>Pi||v.throttle< -1||v.throttle>1)return false;
            state.vehicles.push_back(v);
        }
        if(state.occupied< -1||state.occupied>=int(state.vehicles.size()))return false;
        uint32_t pedestrianCount=reader.u32();if(pedestrianCount>512)return false;state.pedestrians.reserve(pedestrianCount);
        for(uint32_t i=0;i<pedestrianCount;++i){Pedestrian p;p.position=reader.vector();p.yaw=reader.real();p.phase=reader.real();p.panic=reader.real();p.health=reader.real();if(!reader.good||!validPosition(p.position)||std::abs(p.yaw)>Pi*2||std::abs(p.phase)>1e9f||p.panic<0||p.panic>10000||p.health<0||p.health>100)return false;state.pedestrians.push_back(p);}
        if(version>=3)state.missionHold=reader.real();
        uint32_t interruptedTrial=0;
        if(version>=4){state.harborSplit.bestTime=reader.real();state.harborSplit.medal=reader.integer();interruptedTrial=reader.u32();}
        if(state.harborSplit.bestTime<0||state.harborSplit.bestTime>=SplitLimit||state.harborSplit.medal<0||state.harborSplit.medal>3||interruptedTrial>1)return false;
        if((state.harborSplit.bestTime==0)!=(state.harborSplit.medal==0)||(state.harborSplit.bestTime>0&&splitMedal(state.harborSplit.bestTime)!=state.harborSplit.medal))return false;
        if(interruptedTrial&&state.activeMission>=0)return false;
        if(!reader.good||reader.offset!=bytes.size()||state.missionHold<0||state.missionHold>3||(state.missionHold>0&&(state.activeMission!=4||state.missionStage!=1)))return false;
        if(state.occupied>=0&&planarDistance(state.player,state.vehicles[size_t(state.occupied)].position)>3)return false;
        if(version==1){if(state.vehicles.size()>254)return false;appendStarterCraft(state.vehicles,state.world);}
        state.world.stream(state.player);
        for(size_t i=0;i<state.vehicles.size();++i)state.trafficTargets.push_back(nextTrafficTarget(state.world,state.vehicles[i],uint32_t(i)*719));
        for(size_t i=0;i<state.pedestrians.size();++i)state.pedestrianTargets.push_back(pedestrianCorner(state.world,state.pedestrians[i].position,uint32_t(i)*37));
        state.message=interruptedTrial?"Save restored. Harbor Split's interrupted run was cancelled; your records are safe. Return to Rafi's violet flag to retry.":"Save restored. Welcome back to Meridian Coast.";state.messageTime=interruptedTrial?10.0f:5.0f;
        *this=std::move(state);return true;
    }catch(...){return false;}
}
}
