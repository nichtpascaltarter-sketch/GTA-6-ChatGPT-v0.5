#include "../src/game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool close(float a,float b,float tolerance=.001f){return std::abs(a-b)<=tolerance;}
bool same(mc::Vec3 a,mc::Vec3 b){return mc::length(a-b)<.001f;}
void tick(mc::Game& game,const mc::Input& input={},int frames=1){for(int i=0;i<frames;++i)game.update(input,1.f/60,false);}
mc::Game fixture(){
    mc::Game game;game.player={0,game.world.height(0,0),0};game.health=80;game.world.stream(game.player);
    game.pedestrians.resize(4);for(auto& p:game.pedestrians)p.health=0;
    game.pedestrians[0].health=100;game.pedestrians[0].position={0,game.world.height(0,16),16};game.pedestrians[0].yaw=mc::Pi;
    return game;
}
const mc::LawUnitState& officer(const mc::Game& game,uint32_t identity=1){
    const auto& state=game.lawState();
    for(uint32_t i=0;i<state.count;++i)if(state.units[i].identity==identity)return state.units[i];
    throw std::runtime_error("officer missing from integrated law roster");
}
void wall(mc::Game& game){game.world.chunks.front().solids.push_back({{-100,-2,4},{100,5,8}});}
bool sameMemory(const mc::LawMemory& a,const mc::LawMemory& b){
    return a.valid==b.valid&&a.kind==b.kind&&a.serial==b.serial&&a.observer==b.observer&&same(a.position,b.position)&&
           same(a.velocity,b.velocity)&&close(a.age,b.age)&&close(a.uncertainty,b.uncertainty);
}
void equivalentLaw(const mc::LawState& a,const mc::LawState& b){
    require(a.count==b.count&&a.wanted==b.wanted&&a.lastEvidenceSerial==b.lastEvidenceSerial&&
            a.schema==b.schema&&a.searchedSlots==b.searchedSlots&&a.decisionCursor==b.decisionCursor&&a.weaponCursor==b.weaponCursor&&
            close(a.alertRemaining,b.alertRemaining)&&sameMemory(a.shared,b.shared),"hidden player changed shared law state");
    for(uint32_t i=0;i<a.count;++i){const auto& x=a.units[i];const auto& y=b.units[i];
        require(x.identity==y.identity&&x.kind==y.kind&&x.phase==y.phase&&x.hasGoal==y.hasGoal&&same(x.home,y.home)&&same(x.goal,y.goal)&&
                sameMemory(x.memory,y.memory)&&sameMemory(x.radio,y.radio)&&x.searchSlot==y.searchSlot&&
                x.searchSerial==y.searchSerial&&x.searchCursor==y.searchCursor&&
                close(x.sightExposure,y.sightExposure)&&close(x.relayDelay,y.relayDelay)&&close(x.decisionDelay,y.decisionDelay)&&
                close(x.scanTime,y.scanTime)&&close(x.pathRetry,y.pathRetry)&&
                x.weapon.rounds==y.weapon.rounds&&close(x.weapon.reload,y.weapon.reload)&&x.weapon.shotsFired==y.weapon.shotsFired&&
                close(x.weapon.aimTime,y.weapon.aimTime)&&close(x.weapon.cooldown,y.weapon.cooldown),
                "hidden player changed an officer's knowledge, goal, or weapon state");
    }
}

struct TemporarySave {
    std::filesystem::path path;
    TemporarySave(){static uint64_t sequence=0;path=std::filesystem::temp_directory_path()/("meridian_police_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"_"+std::to_string(++sequence)+".sav");}
    ~TemporarySave(){std::error_code error;std::filesystem::remove(path,error);auto temporary=path;temporary+=".tmp";std::filesystem::remove(temporary,error);}
};
using Bytes=std::vector<uint8_t>;
uint32_t word(const Bytes& bytes,size_t offset){require(offset+4<=bytes.size(),"save fixture read out of bounds");uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(bytes[offset+i])<<(8*i);return value;}
void word(Bytes& bytes,size_t offset,uint32_t value){require(offset+4<=bytes.size(),"save fixture write out of bounds");for(unsigned i=0;i<4;++i)bytes[offset+i]=uint8_t(value>>(8*i));}
Bytes read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);require(bool(file),"could not read save");return Bytes(std::istreambuf_iterator<char>(file),{});}
void write(const std::filesystem::path& path,const Bytes& bytes){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));require(bool(file),"could not write save fixture");}
void checksum(Bytes& bytes){
    require(bytes.size()>=20,"save fixture header truncated");word(bytes,12,uint32_t(bytes.size()-20));uint32_t crc=0xffffffffu;
    for(size_t i=20;i<bytes.size();++i){crc^=bytes[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}word(bytes,16,~crc);
}
size_t prefix4(const Bytes& bytes){const size_t vehicles=word(bytes,112);require(vehicles<=256,"invalid fixture vehicle count");const size_t people=116+vehicles*76;const size_t end=people+4+size_t(word(bytes,people))*28+16;require(end<=bytes.size(),"truncated legacy prefix");return end;}
size_t prefix5(const Bytes& bytes){
    const size_t beginning=prefix4(bytes);const size_t count=word(bytes,beginning+4);require(count<=100,"invalid fixture pedestrian count");size_t end=beginning+8;
    for(size_t i=0;i<count;++i){const size_t route=word(bytes,end+124);require(route<=64,"invalid fixture route count");end+=128+route*4;require(end<=bytes.size(),"truncated pedestrian record");}return end;
}
Bytes legacy(Bytes bytes,uint32_t version){
    require(version>=1&&version<=5&&word(bytes,8)==6,"legacy conversion requires a v6 source");
    const size_t fourth=prefix4(bytes),fifth=prefix5(bytes);
    if(version==5)bytes.resize(fifth);
    else if(version==4)bytes.resize(fourth);
    else if(version==3)bytes.resize(fourth-12);
    else if(version==2)bytes.resize(fourth-16);
    else{
        const size_t count=word(bytes,112),tail=116+count*76;
        Bytes old(bytes.begin(),bytes.begin()+116);
        for(size_t i=0;i<count;++i)old.insert(old.end(),bytes.begin()+116+i*76,bytes.begin()+116+i*76+64);
        old.insert(old.end(),bytes.begin()+tail,bytes.begin()+fourth-16);bytes=std::move(old);
    }
    word(bytes,8,version);checksum(bytes);return bytes;
}

void actualSightAndHiddenPlayerInvariance(){
    auto base=fixture();base.wanted=1;
    tick(base,{},30);
    require(officer(base).memory.valid&&officer(base).memory.kind==mc::LawEvidenceKind::Sight,
            "visible wanted player did not produce an actual sight observation");
    wall(base);auto a=base,b=base;
    a.player={-20,a.world.height(-20,0),0};b.player={20,b.world.height(20,0),0};
    for(int frame=0;frame<180;++frame){
        tick(a);tick(b);equivalentLaw(a.lawState(),b.lawState());
        require(same(a.pedestrians[0].position,b.pedestrians[0].position)&&close(a.pedestrians[0].yaw,b.pedestrians[0].yaw),
                "officer followed the hidden player's current position");
        require(a.lawShots().empty()&&b.lawShots().empty(),"officer fired through cover after losing sight");
    }
    require(!same(a.player,b.player),"hidden-player comparison failed to separate the scenarios");
    a.wanted=0;tick(a);
    require(a.lawState().wanted==0&&!a.lawState().shared.valid&&a.lawShots().empty(),"clearing wanted retained shared police evidence");
    for(uint32_t i=0;i<a.lawState().count;++i)require(!a.lawState().units[i].memory.valid,"clearing wanted retained a private pursuit memory");
}

void actualGunshotHearing(){
    for(bool near:{true,false}){
        auto game=fixture();game.pedestrians[0].position={0,game.world.height(0,near?16.f:160.f),near?16.f:160.f};
        game.pedestrians[0].yaw=0;game.yaw=-mc::Pi*.5f;
        mc::Input fire;fire.fire=true;const int ammo=game.ammo;tick(game,fire);tick(game,{},2);
        require(game.ammo==ammo-1,"real gunfire failed to consume ammunition");
        require((game.wanted>0)==near,"inaudible gunfire raised an alert or audible gunfire was ignored");
        const auto& memory=officer(game).memory;
        require(memory.valid==near,"integrated gunshot hearing ignored listener distance");
        if(near)require(memory.kind==mc::LawEvidenceKind::Gunshot,"hearing behind the officer became direct sight");
        else require(!game.lawState().shared.valid,"inaudible gunfire disclosed the player's exact location");
    }
}

void damageRequiresDiscreteShots(){
    auto game=fixture();game.wanted=1;uint32_t shots=0,damageFrames=0;
    for(int frame=0;frame<180;++frame){
        const float before=game.health;tick(game);shots+=uint32_t(game.lawShots().size());
        if(game.health<before){++damageFrames;require(!game.lawShots().empty(),"police proximity dealt damage without a shot");}
        if(frame<30)require(close(game.health,80)&&game.lawShots().empty(),"officer bypassed exposure and aiming delay");
    }
    require(shots>0&&damageFrames>0&&game.health<80,"visible officer never fired a damaging shot");
    require(damageFrames<=shots&&damageFrames<20,"police damage remained continuous instead of shot-based");
}

void interposedBodiesAndVehicles(){
    for(bool person:{true,false}){
        auto game=fixture();game.wanted=1;
        if(person){game.pedestrians.resize(5);game.pedestrians[4].health=100;}
        else{mc::Vehicle car;car.position={0,game.world.height(0,8),8};car.parked=true;game.vehicles.push_back(car);}
        for(int frame=0;frame<180;++frame){
            if(person)game.pedestrians[4].position={0,game.world.height(0,8),8};
            tick(game);
            require(game.lawShots().empty(),"officer fired through an interposed live body or vehicle");
            require(close(game.health,80),"interposed object did not protect the player");
        }
        if(person)require(close(game.pedestrians[4].health,100),"police fired into an interposed civilian");
        else require(close(game.vehicles[0].health,100),"police fired into an opaque bystander vehicle");
    }
}

void friendlyOfficerBlocksRearFire(){
    for(float frontZ:{8.f,15.6f}){
    auto game=fixture();game.wanted=1;game.pedestrians[1].health=100;
    game.pedestrians[1].position={0,game.world.height(0,frontZ),frontZ};game.pedestrians[1].yaw=mc::Pi;
    uint32_t frontShots=0;
    for(int frame=0;frame<180;++frame){
        // Keep the two LOS fixtures stationary while the actual perception and
        // weapon clocks advance; path finding is covered independently.
        game.pedestrians[0].position={0,game.world.height(0,16),16};
        game.pedestrians[1].position={0,game.world.height(0,frontZ),frontZ};
        tick(game);
        for(const auto& shot:game.lawShots()){
            require(shot.shooter!=1,"rear officer fired through a fellow officer");
            if(shot.shooter==2)++frontShots;
        }
        require(close(game.pedestrians[0].health,100)&&close(game.pedestrians[1].health,100),"officers damaged one another");
    }
    if(!(frontShots>0&&game.health<80))throw std::runtime_error("friendly-fire fixture: z="+std::to_string(frontZ)+" shots="+std::to_string(frontShots)+" health="+std::to_string(game.health)+" phase="+std::to_string(int(officer(game,2).phase))+" memory="+std::to_string(officer(game,2).memory.valid)+" rearZ="+std::to_string(game.pedestrians[0].position.z)+" frontZ="+std::to_string(game.pedestrians[1].position.z));
    }
}

void naturalOfficerApproachKeepsSeparation(){
    auto game=fixture();game.wanted=1;game.pedestrians[1].health=100;
    game.pedestrians[1].position={0,game.world.height(0,15.2f),15.2f};game.pedestrians[1].yaw=mc::Pi;
    uint32_t frontShots=0;
    for(int frame=0;frame<180;++frame){
        tick(game);
        const float separation=mc::length(game.pedestrians[0].position-game.pedestrians[1].position);
        if(separation<.68f)throw std::runtime_error("officers overlapped while following shared evidence: "+std::to_string(separation));
        for(const auto& shot:game.lawShots())if(shot.shooter==2)++frontShots;
        require(close(game.pedestrians[0].health,100)&&close(game.pedestrians[1].health,100),"approaching officers damaged each other");
    }
    require(frontShots>0&&game.health<80,"approaching group never completed front-officer fire");
}

void occupiedVehicleAbsorbsShots(){
    auto game=fixture();mc::Vehicle car;car.position=game.player;car.parked=true;game.vehicles.push_back(car);game.occupied=0;game.wanted=1;
    uint32_t shots=0;
    for(int frame=0;frame<180;++frame){tick(game);shots+=uint32_t(game.lawShots().size());}
    require(shots>0,"occupied target vehicle prevented all suspect observations");
    require(game.vehicles[0].health<100,"emitted police rays failed to hit the occupied vehicle");
    require(close(game.health,80),"police rays ignored the vehicle hull and damaged its occupant first");
}

void occupiedPatrolKeepsPlayerControl(){
    auto game=fixture();for(auto& p:game.pedestrians)p.health=0;
    mc::Vehicle patrol;patrol.position={0,game.world.height(0,0),0};patrol.yaw=mc::Pi*.5f;patrol.police=true;patrol.parked=true;game.vehicles.push_back(patrol);
    game.player={0,game.world.height(0,-2),-2};mc::Input enter;enter.interact=true;tick(game,enter);
    require(game.occupied==0&&game.wanted>=2,"taking a patrol car did not board it and report the theft");
    mc::Input drive;drive.moveY=1;tick(game,drive,120);
    require(game.occupied==0&&game.player.x>5&&std::abs(game.player.z)<.5f&&close(game.vehicles[0].yaw,mc::Pi*.5f),
            "police AI overrode the occupied patrol's driving controls");
}

void currentSavePreservesKnowledgeAndCooldown(){
    auto game=fixture();game.wanted=1;
    for(int frame=0;frame<180&&game.lawShots().empty();++frame)tick(game);
    require(!game.lawShots().empty(),"save fixture never emitted a police shot");
    TemporarySave save;require(game.save(save.path.string()),"could not save active police state");
    const auto bytes=read(save.path);require(word(bytes,8)==6&&prefix5(bytes)<bytes.size(),"save did not append a version6 police payload");
    const auto stored=game.lawState();const float health=game.health;
    require(game.load(save.path.string()),"version6 police save failed to load");equivalentLaw(stored,game.lawState());
    require(game.lawShots().empty()&&close(game.health,health),"load replayed a police shot or damage");
    tick(game);require(game.lawShots().empty()&&close(game.health,health),"load discarded the active weapon cooldown");
    wall(game);tick(game,{},25);const auto searching=game.lawState();
    require(officer(game).memory.valid&&officer(game).memory.age>.2f,"search save fixture retained live sight");
    require(game.save(save.path.string())&&game.load(save.path.string()),"could not round trip remembered police evidence");
    equivalentLaw(searching,game.lawState());require(game.lawShots().empty(),"search load replayed presentation shots");
}

void legacySavesDoNotInventPoliceKnowledge(){
    auto source=fixture();source.wanted=2;tick(source,{},30);source.money=4321;source.completedMissions=1;source.harborSplit.bestTime=80;source.harborSplit.medal=3;
    require(officer(source).memory.valid,"legacy source fixture had no observed player");
    TemporarySave save;require(source.save(save.path.string()),"could not create current migration source");const auto bytes=read(save.path);
    for(uint32_t version=1;version<=5;++version){
        write(save.path,legacy(bytes,version));mc::Game restored;
        require(restored.load(save.path.string()),"legacy save failed to migrate to integrated police state");
        require(restored.wanted==source.wanted&&restored.money==4321&&restored.completedMissions==1&&same(restored.player,source.player),
                "police migration changed wanted severity or existing progress");
        require(!restored.lawState().shared.valid&&restored.lawShots().empty(),"legacy migration invented shared police observations");
        for(uint32_t i=0;i<restored.lawState().count;++i)require(!restored.lawState().units[i].memory.valid,"legacy migration invented direct officer sight");
        if(version>=4)require(restored.harborSplit.medal==3&&close(restored.harborSplit.bestTime,80),"police migration changed time-trial records");
        require(restored.save(save.path.string())&&word(read(save.path),8)==6,"legacy police migration did not upgrade cleanly");
    }
}

void invalidPoliceTailIsAtomic(){
    auto game=fixture();game.wanted=1;tick(game,{},30);TemporarySave save,snapshot;
    require(game.save(save.path.string()),"could not save police corruption fixture");const auto original=read(save.path);
    const auto before=game.lawState();const float health=game.health;
    const size_t tail=prefix5(original),unit=tail+88;
    require(word(original,tail+4)==4&&original.size()==tail+88+4*192,"unexpected police corruption fixture layout");
    std::vector<Bytes> malformed;
    for(bool removeAll:{false,true}){auto bytes=original;bytes.resize(removeAll?tail:bytes.size()-1);malformed.push_back(std::move(bytes));}
    const std::pair<size_t,uint32_t> fields[]={
        {tail,2},{tail+4,17},{tail+12,6},{tail+16,0x7fc00000u},
        {tail+68,0x10000u},{tail+72,4},{tail+76,4},{tail+80,0},{tail+84,4},
        {unit,2},{unit+4,2},{unit+8,7},{unit+12,0x7f800000u},{unit+36,2},
        {unit+40,2},{unit+44,3},{unit+52,2},{unit+56,0x7fc00000u},
        {unit+156,16},{unit+160,16},{unit+168,7},{unit+172,0x3f800000u},
        {unit+176,0x3f800000u},{unit+180,0x40800000u},{unit+188,4}
    };
    for(const auto& [offset,value]:fields){auto bytes=original;word(bytes,offset,value);malformed.push_back(std::move(bytes));}
    for(size_t i=0;i<malformed.size();++i){
        auto& bytes=malformed[i];checksum(bytes);write(save.path,bytes);
        if(game.load(save.path.string()))throw std::runtime_error("invalid police payload with valid checksum accepted: case "+std::to_string(i));
        equivalentLaw(before,game.lawState());require(close(game.health,health),"failed police load changed player health");
        require(game.save(snapshot.path.string())&&read(snapshot.path)==original,"failed police load partially changed saved state");
    }
}

void patrolSaveBindingsRemainCanonical(){
    auto game=fixture();
    for(int i=0;i<5;++i){mc::Vehicle vehicle;vehicle.police=true;vehicle.parked=true;vehicle.position={3.2f,game.world.height(3.2f,32.f+i*32.f),32.f+i*32.f};game.vehicles.push_back(vehicle);}
    tick(game);TemporarySave save,snapshot;
    require(game.save(save.path.string()),"could not save canonical patrol fixture");const auto original=read(save.path);
    const auto before=game.lawState();const size_t tail=prefix5(original);
    require(word(original,tail+4)==8,"five cars did not produce four managed patrols");
    auto malformed=original;bool changed=false;
    for(size_t i=0;i<8;++i){const size_t unit=tail+88+i*192;
        if(word(malformed,unit+4)==uint32_t(mc::LawUnitKind::Patrol)){
            word(malformed,unit+188,4);changed=true;break;
        }
    }
    require(changed,"canonical patrol fixture had no patrol binding");checksum(malformed);write(save.path,malformed);
    require(!game.load(save.path.string()),"v6 loader accepted a patrol binding to an unmanaged fifth police vehicle");
    equivalentLaw(before,game.lawState());
    require(game.save(snapshot.path.string())&&read(snapshot.path)==original,"rejected patrol binding changed active state");
}

void populatedOfficerSpawnsAndLegacyRecovery(){
    mc::Game game;game.initialize();
    const size_t people=game.pedestrians.size(),vehicles=game.vehicles.size();
    require(people>=84&&vehicles>=50,"populated officer fixture omitted the living world");
    for(size_t i=0;i<4;++i)require(!game.world.blocked(game.pedestrians[i].position,.35f),"new-game officer spawns inside a street fixture");
    mc::Vec3 historical=game.vehicles[8].position+mc::Vec3{8,0,0};historical.y=game.world.height(historical.x,historical.z);
    require(game.world.blocked(historical,.35f),"legacy officer fixture no longer reproduces the pole overlap");
    game.pedestrians[0].position=historical;
    TemporarySave save;require(game.save(save.path.string()),"could not save legacy officer overlap fixture");
    write(save.path,legacy(read(save.path),5));require(game.load(save.path.string()),"legacy officer overlap failed to load");
    require(same(game.pedestrians[0].position,historical),"legacy migration silently teleported the officer");
    game.player=historical+mc::Vec3{-24,0,-4};game.player.y=game.world.height(game.player.x,game.player.z);
    require(!game.world.blocked(game.player,.34f),"across-street suspect fixture is obstructed");
    game.pedestrians[0].yaw=std::atan2(game.player.x-historical.x,game.player.z-historical.z);
    game.yaw=-mc::Pi*.5f;game.wanted=0;mc::Input crime;crime.aim=true;crime.fire=true;
    const int ammunition=game.ammo;tick(game,crime);
    bool heard=false;
    for(uint32_t i=0;i<game.lawState().count;++i){const auto& memory=game.lawState().units[i].memory;heard|=memory.valid&&memory.kind==mc::LawEvidenceKind::Gunshot;}
    require(game.ammo==ammunition-1&&game.wanted>0&&heard,
            "recovering a legacy officer discarded the first frame's actual gunshot evidence");
    require(!game.world.blocked(game.pedestrians[0].position,.35f)&&mc::length(game.pedestrians[0].position-historical)<=1.1f,
            "legacy officer did not recover locally from its pole overlap");
    bool observed=false;float traveled=0;
    for(int frame=0;frame<180;++frame){
        const mc::Vec3 before=game.pedestrians[0].position;tick(game);
        traveled+=mc::length(game.pedestrians[0].position-before);
        require(mc::length(game.pedestrians[0].position-before)<.1f,"recovered officer teleported during natural pursuit");
        const auto& memory=officer(game).memory;observed|=memory.valid&&memory.kind==mc::LawEvidenceKind::Sight;
    }
    require(observed&&traveled>.02f,"recovered officer never moved toward and acquired the visible across-street suspect");
    require(game.pedestrians.size()==people&&game.vehicles.size()==vehicles,"officer recovery discarded the surrounding population");
}

void pauseClearsShotOutput(){
    auto game=fixture();game.wanted=1;
    for(int frame=0;frame<180&&game.lawShots().empty();++frame)tick(game);
    require(!game.lawShots().empty(),"pause fixture never emitted a police shot");
    const auto frozen=game.lawState();const float health=game.health,time=game.time;
    game.paused=true;tick(game,{},10);
    equivalentLaw(frozen,game.lawState());
    require(close(game.health,health)&&close(game.time,time)&&game.lawShots().empty(),"paused update replayed a police shot or advanced state");
}
}
int main(int argc,char** argv){
    struct Test{const char* name;void(*run)();};
    const Test tests[]={{"actual sight and hidden-player invariance",actualSightAndHiddenPlayerInvariance},{"actual gunshot hearing",actualGunshotHearing},{"damage requires discrete shots",damageRequiresDiscreteShots},{"interposed bodies and vehicles",interposedBodiesAndVehicles},{"friendly officer blocks rear fire",friendlyOfficerBlocksRearFire},{"natural officer approach preserves spacing",naturalOfficerApproachKeepsSeparation},{"occupied vehicle absorbs shots",occupiedVehicleAbsorbsShots},{"occupied patrol retains controls",occupiedPatrolKeepsPlayerControl},{"pause clears shot output",pauseClearsShotOutput},{"current save preserves knowledge and cooldown",currentSavePreservesKnowledgeAndCooldown},{"legacy saves preserve uncertainty",legacySavesDoNotInventPoliceKnowledge},{"invalid police tail is atomic",invalidPoliceTailIsAtomic},{"patrol save bindings remain canonical",patrolSaveBindingsRemainCanonical},{"populated officer spawns and legacy recovery",populatedOfficerSpawnsAndLegacyRecovery}};
    int failures=0,run=0;for(const auto& test:tests){if(argc>1&&std::string(test.name).find(argv[1])==std::string::npos)continue;++run;try{test.run();std::cout<<"PASS "<<test.name<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<'\n';}}
    std::cout<<run-failures<<'/'<<run<<" game police tests passed\n";return failures||run==0?1:0;
}
