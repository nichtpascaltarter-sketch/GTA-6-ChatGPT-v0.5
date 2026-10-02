#include "../src/game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
bool close(float a,float b,float tolerance=.001f) {return std::abs(a-b)<=tolerance;}
bool same(mc::Vec3 a,mc::Vec3 b) {return mc::length(a-b)<.001f;}
bool finite(mc::Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
void budgets(const mc::Game& game) {
    const auto stats=game.pedestrianStats();
    require(stats.decisions<=8,"pedestrian decision budget exceeded");
    require(stats.routeSearches<=2&&stats.routeExpansions<=512,"pedestrian route budget exceeded");
    require(stats.sightChecks<=8,"pedestrian sight-test budget exceeded");
    require(stats.neighborChecks<=16*(game.pedestrians.size()+game.vehicles.size()),
            "pedestrian neighbor-test budget exceeded");
}
void tick(mc::Game& game,const mc::Input& input={},int frames=1) {
    for(int i=0;i<frames;++i) {game.update(input,1.f/30);budgets(game);}
}
void samePedestrian(const mc::Pedestrian& a,const mc::Pedestrian& b) {
    require(same(a.position,b.position)&&close(a.yaw,b.yaw)&&close(a.phase,b.phase)&&
            close(a.panic,b.panic)&&close(a.health,b.health)&&a.identity==b.identity&&
            a.activity==b.activity&&close(a.motion,b.motion)&&close(a.activityTime,b.activityTime)&&
            close(a.sitBlend,b.sitBlend)&&same(a.seatPosition,b.seatPosition)&&a.carrying==b.carrying,
            "paused update changed pedestrian state");
}

void identitiesPauseAndUnloadedResidents() {
    mc::Game game;game.initialize();game.vehicles.clear();tick(game);
    require(game.pedestrianStats().persistentResidents==16,"initialization did not establish 16 persistent residents");
    std::set<uint32_t> identities;
    for(const auto& p:game.pedestrians)
        require(p.identity!=0&&identities.insert(p.identity).second,"pedestrians lack unique nonzero identities");
    const auto frozen=game.pedestrians;const float frozenTime=game.time;
    const auto frozenStats=game.pedestrianStats();
    game.paused=true;mc::Input noisy;noisy.fire=noisy.aim=true;noisy.moveY=1;tick(game,noisy,30);
    require(close(game.time,frozenTime),"pause advanced simulation time");
    for(size_t i=0;i<frozen.size();++i)samePedestrian(game.pedestrians[i],frozen[i]);
    require(game.pedestrianStats().decisions==frozenStats.decisions&&
            game.pedestrianStats().routeSearches==frozenStats.routeSearches,
            "pause changed pedestrian work counters");
    game.paused=false;
    game.player={-4096,game.world.height(-4096,2048),2048};tick(game,{},90);
    require(!game.world.collisionReady(frozen[4].position),"distant fixture retained central collision chunks");
    for(size_t i=4;i<20;++i)
        require(game.pedestrians[i].identity==frozen[i].identity&&same(game.pedestrians[i].position,frozen[i].position),
                "unloaded persistent resident was recycled or moved without collision data");
    bool ambientRecycled=false;
    for(size_t i=20;i<game.pedestrians.size();++i)ambientRecycled|=game.pedestrians[i].identity!=frozen[i].identity;
    require(ambientRecycled,"distant ambient population did not recycle");
    game.player={13.2f,game.world.height(13.2f,13.2f),13.2f};tick(game);
    require(game.pedestrianStats().persistentResidents==16,"return to neighborhood lost its residents");
    for(size_t i=4;i<20;++i)require(game.pedestrians[i].identity==frozen[i].identity,"return changed resident identity");
}

void densePopulationBudgets() {
    mc::Game game;game.initialize();game.vehicles.clear();
    for(size_t i=0;i<128;++i) {
        mc::Vehicle car;car.position={float(i%16)*12-90,0,float(i/16)*12-90};
        car.position.y=game.world.height(car.position.x,car.position.z);car.parked=true;game.vehicles.push_back(car);
    }
    game.pedestrians.resize(128);
    for(size_t i=20;i<game.pedestrians.size();++i) {
        auto& p=game.pedestrians[i];p.identity=0;p.position={13.2f,game.world.height(13.2f,13.2f),13.2f};
    }
    uint32_t searches=0,decisions=0;
    for(int frame=0;frame<60;++frame) {
        tick(game);const auto stats=game.pedestrianStats();searches+=stats.routeSearches;decisions+=stats.decisions;
        for(const auto& p:game.pedestrians)
            require(finite(p.position)&&std::isfinite(p.motion)&&p.motion>=0&&p.motion<=1,
                    "dense neighborhood produced invalid pedestrian state");
    }
    require(searches>0&&decisions>0,"budget fixture never exercised routing or decisions");
}

std::vector<const mc::PedestrianPlace*> places(mc::PedestrianPlaceKind kind) {
    std::vector<const mc::PedestrianPlace*> result;
    for(const auto& place:mc::World::pedestrianPlaces())if(place.kind==kind)result.push_back(&place);
    return result;
}
void tickAt(mc::Game& game,float hour,const mc::Input& input={}) {game.dayTime=hour;tick(game,input);}
void physicalStep(const mc::Game& game,const std::vector<mc::Pedestrian>& before) {
    for(size_t i=4;i<game.pedestrians.size();++i)if(game.pedestrians[i].health>0) {
        require(mc::length(game.pedestrians[i].position-before[i].position)<.16f,
                "resident teleported instead of travelling physically");
        require(!game.world.blocked(game.pedestrians[i].position,.28f),
                "resident walked into static collision geometry");
    }
}

void naturalDailyArrivals() {
    for(float hour:{8.f,19.f}) {
        mc::Game game;game.initialize();game.vehicles.clear();game.pedestrians.resize(20);
        std::array<bool,16> worked{};std::array<bool,2> met{};
        std::array<float,16> travelled{};bool carried=false;int frames=0;
        const int frameLimit=hour==8?5400:3600;
        for(;frames<frameLimit;++frames) {
            const auto before=game.pedestrians;tickAt(game,hour);physicalStep(game,before);
            for(size_t i=4;i<20;++i) {
                const auto& p=game.pedestrians[i];travelled[i-4]+=mc::length(p.position-before[i].position);
                carried|=p.carrying&&p.activity==mc::PedestrianActivity::Carry;
                if(hour==8&&p.activity==mc::PedestrianActivity::Work) {
                    bool atWork=false;for(const auto* site:places(mc::PedestrianPlaceKind::Work))atWork|=mc::length(p.position-site->position)<.3f;
                    require(atWork&&travelled[i-4]>1,"work activity began without walking to an actual work location");worked[i-4]=true;
                }
            }
            for(size_t pair=0;pair<2;++pair) {
                const auto& a=game.pedestrians[4+pair*2];const auto& b=game.pedestrians[5+pair*2];
                if(a.activity==mc::PedestrianActivity::Talk&&b.activity==mc::PedestrianActivity::Talk) {
                    require(mc::length(a.position-b.position)<4,"conversation partners talk from different locations");met[pair]=true;
                }
            }
            if(hour==8&&std::all_of(worked.begin(),worked.end(),[](bool v){return v;}))break;
            if(hour==19&&met[0]&&met[1])break;
        }
        if(hour==8)for(size_t i=4;i<20;++i)if(!worked[i-4]) {
            const auto& p=game.pedestrians[i];std::cerr<<"Unarrived worker "<<p.identity<<" activity "<<int(p.activity)<<" at "<<p.position.x<<","<<p.position.z<<" travelled "<<travelled[i-4]<<'\n';
        }
        if(hour==8)require(std::all_of(worked.begin(),worked.end(),[](bool v){return v;})&&carried,
                          "natural morning schedule failed to deliver all residents to work with a carrying role");
        else require(met[0]&&met[1],"natural evening schedule failed to assemble both conversation groups within 120 seconds");
        std::cout<<"Natural hour "<<hour<<" arrivals in "<<(frames+1)/30.f<<" s\n";
    }
}

void physicalSeatSlots() {
    const auto seats=places(mc::PedestrianPlaceKind::Seat);require(seats.size()>=2,"neighborhood has too few authored seat slots");
    mc::Game game;game.initialize();game.vehicles.clear();game.pedestrians.resize(7);
    for(size_t i=0;i<game.pedestrians.size();++i)if(i!=4&&i!=6)game.pedestrians[i].health=0;
    game.pedestrians[4].position=seats[0]->approach;game.pedestrians[6].position=seats[0]->approach+mc::Vec3{.6f,0,0};
    bool seated=false;
    for(int frame=0;frame<1200;++frame) {
        const auto before=game.pedestrians;tickAt(game,13);physicalStep(game,before);
        if(game.pedestrians[4].activity==mc::PedestrianActivity::Sit&&game.pedestrians[6].activity==mc::PedestrianActivity::Sit) {seated=true;break;}
    }
    if(!seated)for(size_t i:{size_t(4),size_t(6)}) {
        const auto& p=game.pedestrians[i];std::cerr<<"Unsettled sitter "<<p.identity<<" activity "<<int(p.activity)<<" at "<<p.position.x<<","<<p.position.z<<'\n';
    }
    require(seated,"two nearby residents failed to find separate physical seat slots");
    for(int frame=0;frame<30;++frame)tickAt(game,13);
    for(size_t i:{size_t(4),size_t(6)}) {
        const auto& p=game.pedestrians[i];const mc::PedestrianPlace* slot=nullptr;
        for(const auto* seat:seats)if(same(seat->seatPosition,p.seatPosition))slot=seat;
        require(slot&&mc::length(p.position-slot->position)<.3f&&p.sitBlend>.95f&&p.motion<.05f,
                "seated resident did not align with an authored seat and become still");
    }
    require(mc::length(game.pedestrians[4].seatPosition-game.pedestrians[6].seatPosition)>.5f,
            "two residents occupied the same physical seat slot");
}

void conversationInterruptionAndRecovery() {
    const auto meetings=places(mc::PedestrianPlaceKind::Conversation);require(meetings.size()>=2,"conversation fixture has no paired slots");
    mc::Game game;game.initialize();game.vehicles.clear();game.pedestrians.resize(6);
    for(size_t i=0;i<4;++i)game.pedestrians[i].health=0;
    game.pedestrians[4].position=meetings[0]->approach;game.pedestrians[5].position=meetings[1]->approach;
    const auto talking=[&]{return game.pedestrians[4].activity==mc::PedestrianActivity::Talk&&game.pedestrians[5].activity==mc::PedestrianActivity::Talk;};
    for(int frame=0;frame<600&&!talking();++frame)tickAt(game,19);
    require(talking()&&game.pedestrianStats().activeGroups==1,"nearby conversation pair failed to assemble");
    for(size_t i=4;i<6;++i) {
        const auto& p=game.pedestrians[i];const auto& other=game.pedestrians[9-i];
        require(mc::dot(mc::forward(p.yaw),mc::normalized(other.position-p.position))>.8f,
                "talking partners did not face one another");
    }
    game.player=meetings[0]->position+mc::Vec3{0,0,12};game.yaw=0;
    mc::Input fire;fire.fire=true;tickAt(game,19,fire);
    for(int frame=0;frame<12;++frame)tickAt(game,19);
    require(game.pedestrianStats().activeGroups==0&&game.pedestrians[4].panic>0&&game.pedestrians[5].panic>0,
            "gunshot failed to interrupt both members and release the conversation");
    for(int frame=0;frame<1800&&!talking();++frame)tickAt(game,19);
    require(talking()&&game.pedestrianStats().activeGroups==1,
            "conversation did not recover after the threat expired");
    game.pedestrians[5].health=0;
    for(int frame=0;frame<10;++frame)tickAt(game,19);
    require(game.pedestrianStats().activeGroups==0&&game.pedestrians[4].activity!=mc::PedestrianActivity::Talk,
            "surviving resident kept talking to a dead partner");
}

void crossingWaitCommitAndTrafficYield() {
    mc::Game base;base.initialize();base.vehicles.clear();base.pedestrians.resize(5);
    for(size_t i=0;i<4;++i)base.pedestrians[i].health=0;
    base.pedestrians[4].position={-13.2f,base.world.height(-13.2f,16.5f),16.5f};
    base.player={40,base.world.height(40,16.5f),16.5f};
    for(int frame=0;frame<2700&&base.pedestrianStats().crossingWaits==0;++frame)tickAt(base,13);
    require(base.pedestrianStats().crossingWaits==1,"resident never reached a marked crossing curb");
    const auto network=base.world.pedestrianNetwork(base.player);mc::Vec3 from,to;bool found=false;
    for(const auto& edge:network.edges)if(edge.crossingId) {
        const auto a=network.nodes[edge.from].position,b=network.nodes[edge.to].position;
        if(mc::length(base.pedestrians[4].position-a)<.35f){from=a;to=b;found=true;break;}
        if(mc::length(base.pedestrians[4].position-b)<.35f){from=b;to=a;found=true;break;}
    }
    require(found,"crossing wait occurred outside an authored curb");
    const mc::Vec3 center=(from+to)*.5f,crossingDirection=mc::normalized(to-from);
    const float heading=std::abs(to.x-from.x)>std::abs(to.z-from.z)?0:mc::Pi*.5f;
    for(bool parked:{true,false}) {
        mc::Game game=base;mc::Vehicle car;car.yaw=heading;car.parked=parked;car.speed=parked?0:8;
        car.position=center+mc::forward(heading)*(parked?.5f:-14.f)+mc::right(heading)*3.2f;
        car.position.y=game.world.height(car.position.x,car.position.z);car.velocity=mc::forward(heading)*car.speed;
        mc::Vehicle distant;distant.position={300,game.world.height(300,300),300};distant.parked=true;
        game.vehicles.push_back(distant);game.vehicles.push_back(car);
        mc::Game unimpeded=game;unimpeded.pedestrians.clear();
        if(parked) {
            const auto waiting=game.pedestrians[4].position;
            for(int frame=0;frame<60;++frame)tickAt(game,13);
            require(game.pedestrianStats().crossingWaits==1&&same(game.pedestrians[4].position,waiting),
                    "resident entered a crossing blocked by the body of a stationary vehicle");
            game.vehicles.clear();
        }
        bool committed=false,finished=false,yielded=parked;int waitFrames=0;
        for(int frame=0;frame<900;++frame) {
            tickAt(game,13);const auto stats=game.pedestrianStats();
            committed|=stats.committedCrossings>0;if(stats.crossingWaits)++waitFrames;
            if(!parked) {
                tickAt(unimpeded,13);
                yielded|=unimpeded.vehicles[1].speed-game.vehicles[1].speed>1;
            }
            require(close(game.pedestrians[4].health,100),"crossing traffic struck the resident");
            if(committed&&stats.committedCrossings==0&&mc::dot(game.pedestrians[4].position-from,crossingDirection)>mc::length(to-from)-.4f){finished=true;break;}
        }
        if(!committed||!finished||!yielded)std::cerr<<"Crossing parked="<<parked<<" committed="<<committed<<" finished="<<finished<<" yielded="<<yielded<<" position="<<game.pedestrians[4].position.x<<","<<game.pedestrians[4].position.z<<" edge="<<from.x<<","<<from.z<<" -> "<<to.x<<","<<to.z<<" waits="<<game.pedestrianStats().crossingWaits<<" commits="<<game.pedestrianStats().committedCrossings<<'\n';
        require(committed&&finished&&yielded,"marked crossing did not commit, clear, and release yielding traffic");
        if(!parked)require(waitFrames>0,"resident failed to wait for approaching traffic before committing");
    }
}

mc::Game civilianFixture() {
    mc::Game game;
    game.player={0,game.world.height(0,30),30};
    game.pedestrians.resize(5);
    for(size_t i=0;i<4;++i)game.pedestrians[i].health=0;
    game.pedestrians[4].position={0,game.world.height(0,0),0};
    game.world.stream(game.player);
    return game;
}
bool reacting(const mc::Pedestrian& p) {
    return p.panic>0&&(p.activity==mc::PedestrianActivity::Startle||p.activity==mc::PedestrianActivity::Flee);
}

void initialAttachmentDoesNotCutRoads() {
    mc::Game game=civilianFixture();
    const mc::Vec3 start{-10.6f,game.world.height(-10.6f,30),30};
    game.pedestrians[4].position=start;
    auto& solids=game.world.chunks.front().solids;
    solids.push_back({{-30,-1,27.8f},{-10,3,28.2f}});
    solids.push_back({{-30,-1,31.8f},{-10,3,32.2f}});
    solids.push_back({{-11.4f,-1,28},{-11.2f,3,32}});
    require(!game.world.road(start.x,start.z)&&!game.world.blocked(start,.28f),
            "attachment fixture does not leave its resident on clear sidewalk");
    for(int frame=0;frame<90;++frame) {
        tickAt(game,13);
        const auto& p=game.pedestrians[4];
        require(!game.world.road(p.position.x,p.position.z),
                "initial route attachment cut across an unmarked road to the opposite sidewalk");
        require(!game.world.blocked(p.position,.28f),"initial attachment crossed the surrounding fence");
    }
}

void vehicleThreatDirectionAndTimeToCollision() {
    for(int scenario=0;scenario<4;++scenario) {
        mc::Game game=civilianFixture();
        mc::Vehicle car;car.position={scenario==2?6.f:0.f,0,scenario==1?12.f:-12.f};
        car.position.y=game.world.height(car.position.x,car.position.z);
        car.speed=10;car.velocity={0,0,10};game.vehicles.push_back(car);
        if(scenario==3)game.world.chunks.front().solids.push_back({{-5,-1,-6.2f},{5,3,-5.8f}});
        tick(game,{},8);
        if(scenario==0) {
            require(reacting(game.pedestrians[4]),"civilian ignored an approaching collision threat");
            require(close(game.pedestrians[4].health,100),"TTC reaction occurred only after the vehicle struck the civilian");
            const mc::Vec3 before=game.pedestrians[4].position;game.vehicles.clear();tick(game,{},24);
            const mc::Vec3 movement=game.pedestrians[4].position-before;
            require(movement.z>.5f,"civilian fled from the player instead of the approaching vehicle source");
        } else {
            require(!reacting(game.pedestrians[4]),
                    "receding, parallel, or wall-occluded vehicle incorrectly frightened a civilian");
        }
    }
    for (bool approaching : {true, false}) {
        mc::Game game = civilianFixture();
        mc::Vehicle car;
        car.position = {approaching ? -12.f : -8.f, 0, 0};
        car.position.y = game.world.height(car.position.x, car.position.z);
        car.yaw = approaching ? 0 : mc::Pi * .5f;
        car.speed = approaching ? 0 : 12.f;
        car.velocity = {approaching ? 20.f : -20.f, 0, 0};
        game.vehicles.push_back(car);
        game.occupied = 0;
        game.player = car.position;
        mc::Input brake; brake.brake = true;
        tick(game, brake, 8);
        require(close(game.pedestrians[4].health, 100), "sliding-car threat fixture hit the pedestrian");
        require(reacting(game.pedestrians[4]) == approaching,
                "sliding-car reaction followed the hood direction instead of actual velocity");
    }
}

void gunshotHearingAndVisibility() {
    for(int scenario=0;scenario<3;++scenario) {
        mc::Game game=civilianFixture();
        game.player={0,game.world.height(0,scenario==2?-10.f:-30.f),scenario==2?-10.f:-30.f};
        game.yaw=mc::Pi*.5f;
        if(scenario!=0) {
            const float wallZ=scenario==2?-5.f:-15.f;
            game.world.chunks.front().solids.push_back({{-4,-1,wallZ-.2f},{4,3,wallZ+.2f}});
        }
        mc::Input fire;fire.fire=true;tick(game,fire);tick(game,{},8);
        require(close(game.pedestrians[4].health,100),"gunshot hearing fixture accidentally shot its civilian");
        require(reacting(game.pedestrians[4])==(scenario!=1),
                "gunshot response ignored short-range hearing or distant wall occlusion");
    }
    mc::Game carrier;
    carrier.initialize();
    carrier.vehicles.clear();
    carrier.pedestrians.resize(5);
    carrier.dayTime = 8;
    for (int frame = 0; frame < 60 && !carrier.pedestrians[4].carrying; ++frame) tick(carrier);
    require(carrier.pedestrians[4].activity == mc::PedestrianActivity::Carry && carrier.pedestrians[4].carrying,
            "morning resident did not begin carrying a real work parcel");
    carrier.player = carrier.pedestrians[4].position + mc::Vec3{0, 0, 10};
    carrier.player.y = carrier.world.height(carrier.player.x, carrier.player.z);
    carrier.yaw = mc::Pi * .5f;
    mc::Input fire; fire.fire = true;
    tick(carrier, fire);
    bool startled = false, fled = false;
    for (int frame = 0; frame < 60; ++frame) {
        tick(carrier);
        const auto& resident = carrier.pedestrians[4];
        startled |= resident.activity == mc::PedestrianActivity::Startle;
        fled |= resident.activity == mc::PedestrianActivity::Flee;
        require(close(resident.health, 100), "parcel reaction fixture shot the carrier");
        if (reacting(resident)) require(resident.carrying, "frightened carrier lost its existing parcel");
    }
    require(startled && fled, "parcel carrier did not visibly startle and flee after the gunshot");
}
}

int main() {
    struct Test {const char* name;void(*run)();};
    const Test tests[]={
        {"identities, pause, and unloaded residents",identitiesPauseAndUnloadedResidents},
        {"dense population work budgets",densePopulationBudgets},
        {"natural daily arrivals",naturalDailyArrivals},
        {"physical seat slot occupancy",physicalSeatSlots},
        {"conversation interruption and recovery",conversationInterruptionAndRecovery},
        {"crossing wait, commitment, and traffic yield",crossingWaitCommitAndTrafficYield},
        {"initial attachment respects roads",initialAttachmentDoesNotCutRoads},
        {"vehicle threat source and TTC",vehicleThreatDirectionAndTimeToCollision},
        {"gunshot hearing and visibility",gunshotHearingAndVisibility},
    };
    int failures=0;
    for(const auto& test:tests) {
        try {test.run();std::cout<<"PASS "<<test.name<<'\n';}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<'\n';}
    }
    std::cout<<std::size(tests)-failures<<'/'<<std::size(tests)<<" pedestrian tests passed\n";
    return failures?1:0;
}
