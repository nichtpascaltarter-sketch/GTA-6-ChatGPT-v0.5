#include "/workspace/GTA-6-ChatGPT-v0.5-routines/src/world.h"
#include "/workspace/GTA-6-ChatGPT-v0.5-routines/src/pedestrians.h"
#define private public
#include "/workspace/GTA-6-ChatGPT-v0.5-routines/src/game.h"
#undef private
#include <cstdio>
using namespace mc;
int main(){
 Game base;base.initialize();base.vehicles.clear();
 auto graph=base.world.pedestrianNetwork(base.player);
 const PedestrianPlace* seat=nullptr;for(const auto& p:World::pedestrianPlaces())if(p.kind==PedestrianPlaceKind::Seat){seat=&p;break;}
 {
  Game g=base;auto& p=g.pedestrians[4];auto& b=g.pedestrianSimulation.brains[4];p.position=seat->position;p.activity=PedestrianActivity::Sit;p.sitBlend=1;p.seatPosition=seat->seatPosition+Vec3{20,0,0};b.reserved=b.destination=seat->id;b.dwell=10;
  const bool saved=g.save("/tmp/review-seat.save");Game restored;std::printf("bad_seat_saved=%d loaded=%d\n",saved,restored.load("/tmp/review-seat.save"));
 }
 {
  Game g=base;auto& b=g.pedestrianSimulation.brains[4];b.home=g.pedestrianSimulation.brains[5].home;
  const bool saved=g.save("/tmp/review-home.save");Game restored;std::printf("duplicate_home_saved=%d loaded=%d\n",saved,restored.load("/tmp/review-home.save"));
 }
 {
  Game g=base;
  auto& owner=g.pedestrians[4];auto& ob=g.pedestrianSimulation.brains[4];owner.position=seat->position;owner.activity=PedestrianActivity::Sit;owner.seatPosition=seat->seatPosition;owner.sitBlend=1;ob.destination=ob.reserved=seat->id;ob.dwell=20;
  auto& walker=g.pedestrians[5];auto& wb=g.pedestrianSimulation.brains[5];walker.position=seat->position;walker.activity=PedestrianActivity::Walk;wb.destination=seat->id;wb.reserved=0;wb.node=seat->id|0x80000000u;wb.route={wb.node};wb.routeOffset=0;wb.decisionDelay=.5f;
  const bool saved=g.save("/tmp/review-unreserved.save");Game restored;const bool loaded=restored.load("/tmp/review-unreserved.save");
  std::printf("unreserved_route_saved=%d loaded=%d",saved,loaded);
  if(loaded){restored.update(Input{},1.f/60,false);std::printf(" duplicate_sitters=%d owner_res=%u intruder_res=%u",restored.pedestrians[4].activity==PedestrianActivity::Sit&&restored.pedestrians[5].activity==PedestrianActivity::Sit,restored.pedestrianSimulation.brains[4].reserved,restored.pedestrianSimulation.brains[5].reserved);}
  std::puts("");
 }
 for(auto kind:{PedestrianPlaceKind::Seat,PedestrianPlaceKind::Work,PedestrianPlaceKind::Conversation}){
  Game g=base;const PedestrianPlace* spot=nullptr;for(const auto& x:World::pedestrianPlaces())if(x.kind==kind){spot=&x;break;}
  auto& p=g.pedestrians[4];auto& b=g.pedestrianSimulation.brains[4];p.position=spot->position;p.activity=kind==PedestrianPlaceKind::Seat?PedestrianActivity::Sit:(kind==PedestrianPlaceKind::Work?PedestrianActivity::Work:PedestrianActivity::Talk);p.seatPosition=spot->seatPosition;p.sitBlend=kind==PedestrianPlaceKind::Seat?1.f:0.f;p.health=0;b.destination=b.reserved=spot->id;b.dwell=20;
  g.frightenPedestrian(4,p.position,12);
  const bool saved=g.save("/tmp/review-dead.save");Game restored;const bool loaded=restored.load("/tmp/review-dead.save");
  std::printf("dead_place=%d saved=%d loaded=%d activity=%d reserved=%u\n",int(kind),saved,loaded,int(p.activity),b.reserved);
 }
 for(bool far:{false,true}){
  Game g=base;auto& p=g.pedestrians[4];auto& b=g.pedestrianSimulation.brains[4];
  const uint32_t first=(64u<<16)|(64u<<8),last=(65u<<16)|(64u<<8);
  b.node=first;b.route={first,last};b.routeOffset=1;b.destination=b.reserved=b.home;b.crossingFrom=first;b.crossingTo=last;b.crossingCommitted=true;b.decisionDelay=.5f;
  p.position={13.2f,0,13.2f};
  if(far){g.player={4096,0,2048};g.world.stream(g.player);}
  const bool saved=g.save("/tmp/review-route.save");Game restored;const bool loaded=restored.load("/tmp/review-route.save");
  std::printf("bad_route_far=%d saved=%d loaded=%d",far,saved,loaded);
  if(loaded&&far){restored.player={8,0,8};restored.world.stream(restored.player);Input in;restored.update(in,1.f/60);const auto& rb=restored.pedestrianSimulation.brains[4];std::printf(" after_return_route=%zu crossing=%d",rb.route.size(),rb.crossingCommitted);}
  std::puts("");
 }
 {
  Game g=base;g.pedestrians.resize(5);g.pedestrianSimulation.brains.resize(5);
  for(size_t i=0;i<4;++i)g.pedestrians[i].health=0;
  g.pedestrianSimulation.network=graph;g.pedestrianSimulation.revision=g.world.pedestrianResidency();
  for(const auto& edge:graph.edges){
   const auto a=graph.nodes[edge.from],z=graph.nodes[edge.to];const auto middle=(a.position+z.position)*.5f;
   if(!edge.crossingId||std::abs(middle.x-128)>.1f||std::abs(middle.z-16.5f)>.1f)continue;
   auto& p=g.pedestrians[4];auto& b=g.pedestrianSimulation.brains[4];p.position=a.position;p.panic=0;p.activity=PedestrianActivity::Wait;
   b.node=a.id;b.route={a.id,z.id};b.routeOffset=1;b.crossingFrom=a.id;b.crossingTo=z.id;b.crossingCommitted=false;b.decisionDelay=0;
   Vehicle car;car.position={131.2f,0,17};car.yaw=0;car.speed=0;car.parked=true;g.vehicles={car};
   for(int frame=0;frame<60;++frame)g.update(Input{},1.f/60,false);
   std::printf("parked_overlap_waited=%d moved=%.3f",!b.crossingCommitted,length(p.position-a.position));
   g.vehicles[0].position={3000,0,3000};
   for(int frame=0;frame<60;++frame)g.update(Input{},1.f/60,false);
   std::printf(" cleared_then_moved=%.3f\n",length(p.position-a.position));break;
  }
 }

}
