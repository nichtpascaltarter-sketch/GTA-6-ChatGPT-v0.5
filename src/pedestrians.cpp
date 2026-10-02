#include "game.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace mc {
namespace {
float distance2d(Vec3 a,Vec3 b){a.y=b.y=0;return length(a-b);}
const PedestrianPlace* place(uint32_t id){
    for(const auto& p:World::pedestrianPlaces())if(p.id==id)return &p;
    return nullptr;
}
int nodeIndex(const PedestrianNetwork& network,uint32_t id){
    for(size_t i=0;i<network.nodes.size();++i)if(network.nodes[i].id==id)return int(i);
    return -1;
}
uint32_t crossingBetween(const PedestrianNetwork& network,uint32_t from,uint32_t to){
    const int a=nodeIndex(network,from),b=nodeIndex(network,to);if(a<0||b<0)return 0;
    for(const auto& edge:network.edges)if((edge.from==uint32_t(a)&&edge.to==uint32_t(b))||(edge.from==uint32_t(b)&&edge.to==uint32_t(a)))return edge.crossingId;
    return 0;
}
bool directPath(const World& world,Vec3 from,Vec3 to){
    const Vec3 result=world.move(from,to-from,.28f);
    return distance2d(result,to)<.05f&&!world.blocked(to,.28f);
}
bool attachmentPath(const World& world,Vec3 from,Vec3 to){
    const float distance=distance2d(from,to);
    if(distance>36)return false;
    bool leftRoad=!world.road(from.x,from.z);
    const int samples=std::max(1,int(std::ceil(distance)));
    for(int i=1;i<=samples;++i){
        const Vec3 p=lerp(from,to,float(i)/float(samples));
        if(world.road(p.x,p.z)){if(leftRoad)return false;}
        else leftRoad=true;
    }
    return leftRoad&&directPath(world,from,to);
}
bool visible(const World& world,Vec3 a,Vec3 b){
    const Vec3 d=b-a;
    for(const auto& chunk:world.chunks)for(const auto& box:chunk.solids){
        float near=0,far=1;
        const float origin[]={a.x,a.y,a.z},direction[]={d.x,d.y,d.z};
        const float lo[]={box.min.x,box.min.y,box.min.z},hi[]={box.max.x,box.max.y,box.max.z};
        bool hit=true;
        for(int k=0;k<3;++k){
            if(std::abs(direction[k])<1e-6f){if(origin[k]<lo[k]||origin[k]>hi[k]){hit=false;break;}}
            else {float t=(lo[k]-origin[k])/direction[k],u=(hi[k]-origin[k])/direction[k];if(t>u)std::swap(t,u);near=std::max(near,t);far=std::min(far,u);if(near>far){hit=false;break;}}
        }
        if(hit&&near<.999f&&far>.001f)return false;
    }
    return true;
}
float cross2(Vec3 a,Vec3 b){return a.x*b.z-a.z*b.x;}
Vec3 groundVelocity(const Vehicle& vehicle){
    Vec3 velocity=vehicle.velocity;velocity.y=0;
    return dot(velocity,velocity)>.01f?velocity:forward(vehicle.yaw)*vehicle.speed;
}
float rayCrossing(Vec3 position,Vec3 direction,Vec3 a,Vec3 b){
    const Vec3 edge=b-a,delta=a-position;const float denominator=cross2(direction,edge);
    if(std::abs(denominator)<1e-5f)return -1;
    const float ahead=cross2(delta,edge)/denominator,along=cross2(delta,direction)/denominator;
    return ahead>=0&&along>=-.1f&&along<=1.1f?ahead:-1;
}
void activity(Pedestrian& pedestrian,PedestrianActivity value){
    if(pedestrian.activity!=value){pedestrian.activity=value;pedestrian.activityTime=0;}
}
int scheduleAt(float hour,uint32_t identity){
    hour=std::fmod(hour+float(identity%4u)*.04f,24.0f);
    if(hour<6||hour>=22)return 0;
    if(hour<12)return 1;
    if(hour<14)return 2;
    if(hour<18)return 3;
    return 4;
}
const std::vector<std::pair<uint32_t,uint32_t>>& residentAssignments(){
    static const auto assignments=[](){
        std::vector<const PedestrianPlace*> homes,works,social;
        for(const auto& p:World::pedestrianPlaces()){
            if(p.kind==PedestrianPlaceKind::Home)homes.push_back(&p);
            if(p.kind==PedestrianPlaceKind::Work)works.push_back(&p);
            if(p.kind==PedestrianPlaceKind::Conversation)social.push_back(&p);
        }
        const auto originalHomes=homes;
        std::vector<const PedestrianPlace*> ordered;
        for(const auto* meeting:social){
            auto nearest=homes.end();float best=1e9f;
            for(auto it=homes.begin();it!=homes.end();++it){const float distance=distance2d((*it)->position,meeting->position);if(distance<best){best=distance;nearest=it;}}
            if(nearest!=homes.end()){ordered.push_back(*nearest);homes.erase(nearest);}
        }
        ordered.insert(ordered.end(),homes.begin(),homes.end());
        std::vector<std::pair<uint32_t,uint32_t>> result;
        for(const auto* home:ordered){
            const auto original=std::find(originalHomes.begin(),originalHomes.end(),home);
            const size_t index=size_t(original-originalHomes.begin());
            if(index<works.size())result.push_back({home->id,works[index]->id});
        }
        return result;
    }();
    return assignments;
}
int nearestNode(const World& world,const PedestrianNetwork& network,Vec3 position){
    std::vector<std::pair<float,int>> candidates;
    for(size_t i=0;i<network.nodes.size();++i){float d=distance2d(position,network.nodes[i].position);if(d<36)candidates.push_back({d,int(i)});}
    std::sort(candidates.begin(),candidates.end());
    for(size_t i=0;i<std::min<size_t>(8,candidates.size());++i)if(attachmentPath(world,position,network.nodes[size_t(candidates[i].second)].position))return candidates[i].second;
    return -1;
}
bool routeTo(const World& world,PedestrianSimulation& simulation,PedestrianBrain& brain,Vec3 position,uint32_t destination){
    if(simulation.stats.routeSearches>=2)return false;
    const auto& network=simulation.network;
    const auto destinationIt=std::find_if(network.places.begin(),network.places.end(),[&](const auto& p){return p.id==destination;});
    if(destinationIt==network.places.end())return false;
    const int start=nearestNode(world,network,position),goal=int(destinationIt->nodeIndex);
    if(start<0||goal<0||size_t(goal)>=network.nodes.size())return false;
    ++simulation.stats.routeSearches;
    std::array<float,256> cost;cost.fill(1e9f);
    std::array<int,256> previous;previous.fill(-1);
    std::array<bool,256> visited{};cost[size_t(start)]=0;
    for(size_t step=0;step<network.nodes.size();++step){
        int current=-1;float best=1e9f;
        for(size_t i=0;i<network.nodes.size();++i)if(!visited[i]&&cost[i]<best){current=int(i);best=cost[i];}
        if(current<0)break;
        ++simulation.stats.routeExpansions;visited[size_t(current)]=true;if(current==goal)break;
        for(const auto& edge:network.edges){
            int next=-1;if(edge.from==uint32_t(current))next=int(edge.to);else if(edge.to==uint32_t(current))next=int(edge.from);
            if(next<0)continue;
            const float candidate=best+distance2d(network.nodes[size_t(current)].position,network.nodes[size_t(next)].position)+(edge.crossingId?6.0f:0.0f);
            if(candidate<cost[size_t(next)]){cost[size_t(next)]=candidate;previous[size_t(next)]=current;}
        }
    }
    if(!visited[size_t(goal)])return false;
    std::vector<uint32_t> route;
    for(int i=goal;i>=0;i=previous[size_t(i)]){route.push_back(network.nodes[size_t(i)].id);if(route.size()>64)return false;if(i==start)break;}
    std::reverse(route.begin(),route.end());
    brain.route=std::move(route);brain.routeOffset=0;brain.node=network.nodes[size_t(start)].id;
    brain.destination=brain.reserved=destination;brain.blockedTime=0;brain.crossingFrom=brain.crossingTo=0;brain.crossingCommitted=false;
    return true;
}
int64_t cellKey(int x,int z){return int64_t(uint64_t(uint32_t(x))<<32|uint32_t(z));}
}

const char* Game::pedestrianActivityName(PedestrianActivity value){
    static const char* names[]={"Walking","Waiting","Seated","Checking stock","Carrying","Talking","Startled","Fleeing"};
    const size_t index=size_t(value);return index<8?names[index]:"Unknown";
}
PedestrianStats Game::pedestrianStats() const{
    PedestrianStats result=pedestrianSimulation.stats;result.activityCounts={};result.persistentResidents=0;
    for(const auto& p:pedestrians)if(p.health>0&&size_t(p.activity)<8)++result.activityCounts[size_t(p.activity)];
    for(const auto& b:pedestrianSimulation.brains)if(b.persistent)++result.persistentResidents;
    return result;
}
bool Game::persistentPedestrian(size_t index) const{return index<pedestrianSimulation.brains.size()&&pedestrianSimulation.brains[index].persistent;}
void Game::recyclePedestrian(size_t index){
    if(index>=pedestrians.size())return;
    pedestrians[index].identity=0;
    if(index<pedestrianSimulation.brains.size())pedestrianSimulation.brains[index]={};
}
void Game::synchronizePedestrians(){
    auto& simulation=pedestrianSimulation;
    simulation.brains.resize(pedestrians.size());
    const auto& assignments=residentAssignments();
    for(size_t i=0;i<pedestrians.size();++i){
        auto& p=pedestrians[i];auto& brain=simulation.brains[i];
        if(p.identity&&brain.identity==p.identity)continue;
        brain={};if(!p.identity)p.identity=simulation.nextIdentity++;
        simulation.nextIdentity=std::max(simulation.nextIdentity,p.identity+1);brain.identity=p.identity;
        brain.decisionDelay=float(i%8u)*.025f;
        if(i>=4&&i<20&&i-4<assignments.size()){
            brain.persistent=true;brain.home=assignments[i-4].first;brain.work=assignments[i-4].second;
            if(i<8)brain.group=uint32_t((i-4)/2+1);
        }
    }
}
void Game::initializePedestrians(bool relocateResidents){
    if(!relocateResidents&&pedestrians.size()>=4){
        // Legacy saves have no persistent identities. Keep each old person intact,
        // reuse reachable people, and fill missing resident roles at authored homes.
        auto legacy=std::move(pedestrians);
        const auto network=world.pedestrianNetwork(player);
        std::vector<bool> used(legacy.size()),reachable(legacy.size());
        for(size_t i=0;i<legacy.size();++i){
            legacy[i].identity=uint32_t(i+1);
            if(i>=4&&legacy[i].health>0&&world.collisionReady(legacy[i].position)&&!world.blocked(legacy[i].position,.28f))
                reachable[i]=nearestNode(world,network,legacy[i].position)>=0;
        }
        pedestrians.clear();pedestrians.reserve(legacy.size()+16);
        for(size_t i=0;i<4;++i){pedestrians.push_back(legacy[i]);used[i]=true;}
        uint32_t nextIdentity=uint32_t(legacy.size()+1);
        const auto& assignments=residentAssignments();
        for(size_t slot=0;slot<std::min<size_t>(16,assignments.size());++slot){
            const auto& assignment=assignments[slot];
            const auto* home=place(assignment.first);if(!home)continue;
            size_t chosen=legacy.size();float best=std::numeric_limits<float>::max();
            for(size_t i=4;i<legacy.size();++i)if(!used[i]&&reachable[i]){
                const float distance=distance2d(legacy[i].position,home->position);
                if(distance<best){best=distance;chosen=i;}
            }
            if(chosen<legacy.size()){pedestrians.push_back(legacy[chosen]);used[chosen]=true;}
            else {
                Pedestrian resident;resident.position=home->position;resident.yaw=home->yaw;
                resident.identity=nextIdentity++;resident.phase=random01(resident.identity)*2*Pi;
                pedestrians.push_back(resident);
            }
        }
        for(size_t i=4;i<legacy.size();++i)if(!used[i])pedestrians.push_back(legacy[i]);
        pedestrianSimulation={};
    }
    synchronizePedestrians();
    if(relocateResidents)for(size_t i=4;i<pedestrians.size();++i){
        const auto& brain=pedestrianSimulation.brains[i];if(!brain.persistent)continue;
        if(const auto* home=place(brain.home)){pedestrians[i].position=home->position;pedestrians[i].yaw=home->yaw;}
    }
    pedestrianSimulation.revision=std::numeric_limits<uint64_t>::max();
}
bool Game::validatePedestrianRoutes(bool rejectInvalid){
    const auto& network=pedestrianSimulation.network;
    for(size_t index=0;index<pedestrianSimulation.brains.size();++index){
        auto& b=pedestrianSimulation.brains[index];
        if(b.route.empty())continue;
        bool missing=nodeIndex(network,b.node)<0,invalid=false;uint32_t previous=b.node;
        for(size_t i=b.routeOffset;i<b.route.size();++i){
            const int a=nodeIndex(network,previous),z=nodeIndex(network,b.route[i]);
            if(a<0||z<0){missing=true;break;}
            if(a!=z&&!std::any_of(network.edges.begin(),network.edges.end(),[&](const auto& edge){return (edge.from==uint32_t(a)&&edge.to==uint32_t(z))||(edge.to==uint32_t(a)&&edge.from==uint32_t(z));}))invalid=true;
            previous=b.route[i];
        }
        const auto destination=std::find_if(network.places.begin(),network.places.end(),[&](const auto& p){return p.id==b.destination;});
        if(destination==network.places.end())missing=true;
        else if(b.route.back()!=network.nodes[destination->nodeIndex].id)invalid=true;
        if(b.crossingFrom&&nodeIndex(network,b.crossingFrom)>=0&&nodeIndex(network,b.crossingTo)>=0&&!crossingBetween(network,b.crossingFrom,b.crossingTo))invalid=true;
        if(!missing&&b.routeOffset<b.route.size()){
            const Vec3 a=network.nodes[size_t(nodeIndex(network,b.node))].position,z=network.nodes[size_t(nodeIndex(network,b.route[b.routeOffset]))].position;
            const Vec3 position=pedestrians[index].position,span=z-a;
            if(b.routeOffset==0&&b.node==b.route[0])invalid|=!attachmentPath(world,position,a);
            else {const Vec3 closest=a+span*clamp(dot(position-a,span)/std::max(.01f,dot(span,span)),0,1);invalid|=distance2d(position,closest)>2;}
        }
        if(missing&&!invalid)continue;
        if(invalid){
            if(rejectInvalid)return false;
            b.route.clear();b.routeOffset=0;b.reserved=0;b.dwell=0;b.crossingFrom=b.crossingTo=0;b.crossingCommitted=false;b.decisionDelay=0;
        }
    }
    return true;
}
void Game::beginPedestrianFrame(float dt){
    auto& simulation=pedestrianSimulation;simulation.stats={};synchronizePedestrians();
    const uint64_t residency=world.pedestrianResidency();
    if(simulation.revision!=residency){simulation.network=world.pedestrianNetwork(player);simulation.revision=residency;validatePedestrianRoutes(false);}
    for(auto& event:simulation.stimuli)event.age+=dt;
    simulation.stimuli.erase(std::remove_if(simulation.stimuli.begin(),simulation.stimuli.end(),[](const auto& e){return e.age>2;}),simulation.stimuli.end());
    simulation.crossings.clear();
    for(const auto& b:simulation.brains)if(b.crossingFrom&&b.crossingTo){
        const uint32_t id=crossingBetween(simulation.network,b.crossingFrom,b.crossingTo);
        if(!id||simulation.crossings.size()>=32)continue;
        if(std::any_of(simulation.crossings.begin(),simulation.crossings.end(),[&](const auto& c){return c.id==id;}))continue;
        const int a=nodeIndex(simulation.network,b.crossingFrom),z=nodeIndex(simulation.network,b.crossingTo);
        simulation.crossings.push_back({id,simulation.network.nodes[size_t(a)].position,simulation.network.nodes[size_t(z)].position});
    }
}
void Game::alertPedestrians(Vec3 position,float radius){
    auto& s=pedestrianSimulation;if(s.stimuli.size()>=16)s.stimuli.erase(s.stimuli.begin());
    s.stimuli.push_back({position,0,radius,++s.stimulusSerial});
}
void Game::frightenPedestrian(size_t index,Vec3 position,float duration){
    if(index<4||index>=pedestrians.size())return;
    auto& p=pedestrians[index];auto& b=pedestrianSimulation.brains[index];
    if(p.health<=0){
        b.reserved=0;b.dwell=0;b.route.clear();b.routeOffset=0;
        b.crossingFrom=b.crossingTo=0;b.crossingCommitted=false;
        p.motion=0;p.sitBlend=0;p.carrying=false;activity(p,PedestrianActivity::Wait);
        return;
    }
    const bool fresh=p.panic<=0||!b.hasThreat;
    const bool redirect=fresh||distance2d(b.threat,position)>4;
    p.panic=std::max(p.panic,duration);b.threat=position;b.hasThreat=true;b.reserved=0;b.dwell=0;
    b.crossingFrom=b.crossingTo=0;b.crossingCommitted=false;b.route.clear();b.routeOffset=0;
    Vec3 away=p.position-position;away.y=0;if(length(away)<.01f)away=right(p.yaw);
    if(redirect){
        b.escape=p.position+normalized(away)*16;
        std::vector<std::pair<float,size_t>> candidates;
        const auto& nodes=pedestrianSimulation.network.nodes;
        for(size_t n=0;n<nodes.size();++n){
            const float near=distance2d(p.position,nodes[n].position);if(near<3||near>24||nodes[n].kind==PedestrianNodeKind::Place)continue;
            candidates.push_back({near*.35f-distance2d(position,nodes[n].position),n});
        }
        std::sort(candidates.begin(),candidates.end());
        for(size_t n=0;n<std::min<size_t>(4,candidates.size());++n)if(directPath(world,p.position,nodes[candidates[n].second].position)){b.escape=nodes[candidates[n].second].position;break;}
    }
    if(fresh){b.reactionTime=.35f;activity(p,PedestrianActivity::Startle);}
    b.escape.x=clamp(b.escape.x,-World::Extent,World::Extent);b.escape.z=clamp(b.escape.z,-World::Extent,World::Extent);
}
float Game::pedestrianTrafficSpeed(const Vehicle& vehicle,float proposed) const{
    for(const auto& crossing:pedestrianSimulation.crossings){
        if(std::abs(vehicle.position.y-crossing.from.y)>2.5f)continue;
        const float ahead=rayCrossing(vehicle.position,forward(vehicle.yaw),crossing.from,crossing.to);
        if(ahead>=0&&ahead<std::max(25.0f,vehicle.speed*vehicle.speed/12+12))proposed=std::min(proposed,std::max(0.0f,(ahead-4)*.7f));
    }
    return proposed;
}

void Game::updateCivilianPedestrians(float dt){
    auto& simulation=pedestrianSimulation;auto& stats=simulation.stats;
    if(pedestrians.size()<=4)return;
    for(size_t i=4;i<pedestrians.size();++i){auto& b=simulation.brains[i];b.decisionDelay=std::max(0.0f,b.decisionDelay-dt);b.dwell=std::max(0.0f,b.dwell-dt);b.reactionTime=std::max(0.0f,b.reactionTime-dt);}
    size_t examined=0;
    while(examined<pedestrians.size()-4&&stats.decisions<8){
        if(simulation.cursor<4||simulation.cursor>=pedestrians.size())simulation.cursor=4;
        const size_t i=simulation.cursor++;++examined;auto& p=pedestrians[i];auto& b=simulation.brains[i];
        if(p.health<=0||b.decisionDelay>0||!world.collisionReady(p.position))continue;
        ++stats.decisions;b.decisionDelay=.16f;
        for(const auto& event:simulation.stimuli)if(event.serial>b.lastStimulus){
            const float d=length(p.position-event.position);
            if(d>event.radius){b.lastStimulus=event.serial;continue;}
            bool heard=d<24;
            if(!heard){
                if(stats.sightChecks>=8)break;
                ++stats.sightChecks;heard=visible(world,pedestrianBodyPoint(p,1.4f),event.position+Vec3{0,1.2f,0});
            }
            b.lastStimulus=event.serial;
            if(heard)frightenPedestrian(i,event.position,10);
        }
        if(aiming&&stats.sightChecks<8&&length(p.position-player)<24){
            const Vec3 toward=normalized(p.position-player);
            if(dot(toward,forward(yaw))>.82f){++stats.sightChecks;if(visible(world,player+Vec3{0,1.4f,0},pedestrianBodyPoint(p,1.4f)))frightenPedestrian(i,player,6);}
        }
        if(b.group&&p.panic<=0)for(size_t other=4;other<simulation.brains.size();++other){
            if(other==i||simulation.brains[other].group!=b.group||pedestrians[other].activity!=PedestrianActivity::Startle||distance2d(p.position,pedestrians[other].position)>8)continue;
            frightenPedestrian(i,simulation.brains[other].threat,6);break;
        }
        for(const auto& vehicle:vehicles){
            ++stats.neighborChecks;const Vec3 velocity=groundVelocity(vehicle);
            if(std::abs(vehicle.position.y-p.position.y)>2.5f||length(velocity)<3)continue;
            Vec3 relative=p.position-vehicle.position;relative.y=0;
            const float t=dot(relative,velocity)/std::max(.01f,dot(velocity,velocity));
            if(t>.03f&&t<1.8f&&length(relative-velocity*t)<1.9f&&distance2d(p.position,vehicle.position)<35){
                if(stats.sightChecks<8){++stats.sightChecks;if(visible(world,p.position+Vec3{0,.8f,0},vehicle.position+Vec3{0,.8f,0}))frightenPedestrian(i,vehicle.position,3);}
            }
        }
        if(p.panic>0){if(!b.hasThreat)frightenPedestrian(i,player,p.panic);continue;}
        b.hasThreat=false;
        if(b.crossingFrom&&!b.crossingCommitted){
            const int from=nodeIndex(simulation.network,b.crossingFrom),to=nodeIndex(simulation.network,b.crossingTo);
            bool safe=from>=0&&to>=0;
            if(safe){
                const auto a=simulation.network.nodes[size_t(from)].position,z=simulation.network.nodes[size_t(to)].position;
                const uint32_t crossing=crossingBetween(simulation.network,b.crossingFrom,b.crossingTo);
                safe=std::any_of(simulation.crossings.begin(),simulation.crossings.end(),[&](const auto& c){return c.id==crossing;});
                for(size_t v=0;v<vehicles.size()&&safe;++v){
                    const auto& car=vehicles[v];++stats.neighborChecks;if(std::abs(car.position.y-p.position.y)>2.5f)continue;
                    Vec3 span=z-a;span.y=0;Vec3 toCar=car.position-a;toCar.y=0;
                    const Vec3 closest=a+span*clamp(dot(toCar,span)/std::max(.01f,dot(span,span)),0,1);
                    const float bodyRadius=car.kind==VehicleKind::Motorcycle?1.3f:(car.kind==VehicleKind::Car?2.7f:4.5f);
                    if(distance2d(car.position,closest)<bodyRadius){safe=false;continue;}
                    const Vec3 velocity=groundVelocity(car);const float speed=length(velocity);
                    const float ahead=rayCrossing(car.position,speed>.01f?velocity/speed:forward(car.yaw),a,z);
                    if(ahead>=0&&ahead<3.0f)safe=false;
                    if(speed>.7f&&ahead>=0&&ahead/speed<(int(v)==occupied?12.0f:3.2f))safe=false;
                }
            }
            if(safe)b.crossingCommitted=true;
            continue;
        }
        if(b.routeOffset<b.route.size()||b.dwell>0)continue;
        const int band=scheduleAt(dayTime,p.identity);b.schedule=band;
        const PedestrianPlace* destination=nullptr;
        if(b.persistent&&(band==0||band==1||band==3))destination=place(band==0?b.home:b.work);
        else {
            PedestrianPlaceKind kind=band==2?((p.identity&1u)?PedestrianPlaceKind::Seat:PedestrianPlaceKind::Market):PedestrianPlaceKind::Shelter;
            bool partner=false;
            if(b.group)for(size_t other=4;other<simulation.brains.size();++other)if(other!=i&&simulation.brains[other].group==b.group&&pedestrians[other].health>0)partner=true;
            if(band==4)kind=partner?PedestrianPlaceKind::Conversation:PedestrianPlaceKind::Seat;
            if(!b.persistent&&band!=2&&band!=4)kind=(p.identity%3u==0)?PedestrianPlaceKind::Market:PedestrianPlaceKind::Seat;
            std::vector<const PedestrianPlace*> options;
            for(const auto& candidate:simulation.network.places)if(candidate.kind==kind)options.push_back(&candidate);
            if(b.group&&kind==PedestrianPlaceKind::Conversation&&options.size()>=4)destination=options[(i-4)%4];
            else {
                float best=1e9f;
                for(const auto* candidate:options){
                    bool reserved=false;for(size_t other=4;other<simulation.brains.size();++other)if(other!=i&&simulation.brains[other].reserved==candidate->id){reserved=true;break;}
                    if(reserved)continue;
                    const float score=distance2d(p.position,candidate->position)+float(hash32(p.identity+candidate->id)%13u);
                    if(score<best){best=score;destination=candidate;}
                }
            }
        }
        if(destination){
            bool reserved=false;for(size_t other=4;other<simulation.brains.size();++other)if(other!=i&&simulation.brains[other].reserved==destination->id){reserved=true;break;}
            if(!reserved&&routeTo(world,simulation,b,p.position,destination->id)){p.carrying=destination->kind==PedestrianPlaceKind::Work&&(p.identity%4u==1);activity(p,p.carrying?PedestrianActivity::Carry:PedestrianActivity::Walk);}
        }
    }
    std::map<int64_t,std::vector<size_t>> bins;
    for(size_t i=4;i<pedestrians.size();++i)if(pedestrians[i].health>0)bins[cellKey(int(std::floor(pedestrians[i].position.x/3)),int(std::floor(pedestrians[i].position.z/3)))].push_back(i);
    std::set<uint32_t> activeGroups;
    for(size_t i=4;i<pedestrians.size();++i){
        auto& p=pedestrians[i];auto& b=simulation.brains[i];if(b.persistent)++stats.persistentResidents;
        if(p.health<=0){
            b.reserved=0;b.dwell=0;b.route.clear();b.routeOffset=0;b.crossingFrom=b.crossingTo=0;b.crossingCommitted=false;
            p.motion=0;p.sitBlend=0;p.carrying=false;activity(p,PedestrianActivity::Wait);continue;
        }
        p.panic=std::max(0.0f,p.panic-dt);p.activityTime+=dt;
        p.sitBlend=lerp(p.sitBlend,p.activity==PedestrianActivity::Sit?1.0f:0.0f,std::min(1.0f,dt*5));
        if(!world.collisionReady(p.position)){p.motion=0;++stats.activityCounts[size_t(p.activity)];continue;}
        Vec3 target=p.position;float speed=0;
        if(p.panic>0){
            if(b.reactionTime>0)activity(p,PedestrianActivity::Startle);
            else {activity(p,PedestrianActivity::Flee);target=b.escape;speed=4.4f;if(distance2d(p.position,target)<1)target=p.position+normalized(p.position-b.threat)*8;}
        }else if(b.routeOffset<b.route.size()){
            const int next=nodeIndex(simulation.network,b.route[b.routeOffset]);
            if(next<0||nodeIndex(simulation.network,b.node)<0){activity(p,PedestrianActivity::Wait);}
            else {
                target=simulation.network.nodes[size_t(next)].position;
                const uint32_t crossing=crossingBetween(simulation.network,b.node,b.route[b.routeOffset]);
                if(crossing&&!b.crossingCommitted){b.crossingFrom=b.node;b.crossingTo=b.route[b.routeOffset];b.crossingWait=std::min(3600.0f,b.crossingWait+dt);activity(p,PedestrianActivity::Wait);}
                else {
                    activity(p,p.carrying?PedestrianActivity::Carry:PedestrianActivity::Walk);speed=b.crossingCommitted?2.2f:1.25f+float(p.identity%7u)*.055f;
                    if(b.group&&b.schedule==4&&!b.crossingCommitted)for(size_t other=4;other<simulation.brains.size();++other){
                        if(other==i||simulation.brains[other].group!=b.group||pedestrians[other].health<=0||pedestrians[other].panic>0)continue;
                        const Vec3 offset=pedestrians[other].position-p.position;const float gap=length(offset);
                        if(gap>3&&gap<10)speed*=dot(offset,normalized(target-p.position))<0?.6f:1.15f;
                    }
                }
                if(distance2d(p.position,target)<.18f){
                    b.node=b.route[b.routeOffset++];b.crossingFrom=b.crossingTo=0;b.crossingCommitted=false;b.crossingWait=0;
                    if(b.routeOffset==b.route.size()){
                        b.route.clear();b.routeOffset=0;b.blockedTime=0;
                        if(const auto* arrived=place(b.destination)){
                            p.yaw=arrived->yaw;p.carrying=false;b.dwell=24+float(p.identity%9u);
                            if(arrived->kind==PedestrianPlaceKind::Seat){activity(p,PedestrianActivity::Sit);p.seatPosition=arrived->seatPosition;}
                            else if(arrived->kind==PedestrianPlaceKind::Work||arrived->kind==PedestrianPlaceKind::Market)activity(p,PedestrianActivity::Work);
                            else if(arrived->kind==PedestrianPlaceKind::Conversation)activity(p,PedestrianActivity::Talk);
                            else activity(p,PedestrianActivity::Wait);
                        }
                        speed=0;
                    }
                }
            }
        }else if(b.dwell<=0){
            b.reserved=0;
            if(b.persistent){activity(p,PedestrianActivity::Wait);}
            else {
                activity(p,PedestrianActivity::Walk);target=pedestrianTargets[i];speed=1.3f;
                if(distance2d(p.position,target)<1){
                    const float grid=std::abs(p.position.x)<1536&&std::abs(p.position.z)<1792?128.0f:256.0f;
                    const float x=std::floor(p.position.x/grid)*grid,z=std::floor(p.position.z/grid)*grid;
                    const bool east=p.position.x-x>grid*.5f,north=p.position.z-z>grid*.5f;
                    target=east?(north?Vec3{x+13.2f,0,z+grid-13.2f}:Vec3{x+grid-13.2f,0,z+grid-13.2f}):(north?Vec3{x+13.2f,0,z+13.2f}:Vec3{x+grid-13.2f,0,z+13.2f});
                    target.y=world.height(target.x,target.z);pedestrianTargets[i]=target;
                }
            }
        }
        const auto* atPlace=place(b.destination);
        if(b.dwell>0&&atPlace&&atPlace->kind==PedestrianPlaceKind::Conversation&&b.group){
            bool companion=false,alive=false;
            for(size_t other=4;other<simulation.brains.size();++other)if(other!=i&&simulation.brains[other].group==b.group&&pedestrians[other].health>0&&pedestrians[other].panic<=0&&distance2d(p.position,pedestrians[other].position)<4){
                const Vec3 toward=pedestrians[other].position-p.position;p.yaw=std::atan2(toward.x,toward.z);companion=true;activeGroups.insert(b.group);break;
            }
            for(size_t other=4;other<simulation.brains.size();++other)if(other!=i&&simulation.brains[other].group==b.group&&pedestrians[other].health>0)alive=true;
            activity(p,companion?PedestrianActivity::Talk:PedestrianActivity::Wait);
            if(!companion)b.dwell=std::min(b.dwell,5.0f);
            if(!alive){b.dwell=0;b.reserved=0;b.destination=0;}
        }
        const Vec3 before=p.position;
        if(speed>0){
            Vec3 direction=target-p.position;direction.y=0;const float remaining=length(direction);direction=normalized(direction);
            Vec3 separation;size_t checked=0;const int cx=int(std::floor(p.position.x/3)),cz=int(std::floor(p.position.z/3));
            for(int z=cz-1;z<=cz+1&&checked<16;++z)for(int x=cx-1;x<=cx+1&&checked<16;++x){
                auto found=bins.find(cellKey(x,z));if(found==bins.end())continue;
                for(size_t other:found->second){
                    if(other==i)continue;
                    if(checked++>=16)break;
                    ++stats.neighborChecks;
                    Vec3 delta=p.position-pedestrians[other].position;delta.y=0;const float distance=length(delta);
                    if(distance>.01f&&distance<.9f){
                        separation+=delta/distance*((.9f-distance)*1.8f);
                        if(dot(direction,delta)<0)separation+=Vec3{direction.z,0,-direction.x}*((.9f-distance)*.9f);
                    }
                }
            }
            direction=normalized(direction+separation);p.yaw=wrapAngle(p.yaw+wrapAngle(std::atan2(direction.x,direction.z)-p.yaw)*std::min(1.0f,dt*7));
            p.position=world.move(p.position,direction*std::min(remaining,speed*dt),.28f);p.position.y=world.height(p.position.x,p.position.z);
            const float moved=distance2d(before,p.position);if(moved<dt*.08f)b.blockedTime+=dt;else b.blockedTime=0;
            if(b.blockedTime>4){b.route.clear();b.routeOffset=0;b.reserved=0;b.dwell=1;b.blockedTime=0;b.decisionDelay=0;b.crossingFrom=b.crossingTo=0;b.crossingCommitted=false;}
        }
        const float moved=distance2d(before,p.position);p.phase+=moved*3;p.motion=lerp(p.motion,std::min(1.0f,moved/std::max(.001f,dt)/2.2f),std::min(1.0f,dt*10));
        if(b.crossingFrom){if(b.crossingCommitted)++stats.committedCrossings;else ++stats.crossingWaits;}
        ++stats.activityCounts[size_t(p.activity)];
    }
    stats.activeGroups=uint32_t(activeGroups.size());
}
}
