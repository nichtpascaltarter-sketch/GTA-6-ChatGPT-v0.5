// Include the authored implementation to inspect its exact private collider helpers.
// Do not also link game_law.o when compiling this diagnostic.
#include "../../../src/game_law.cpp"
#include <iomanip>
#include <iostream>
using namespace mc;
void point(Vec3 p){std::cout<<'('<<p.x<<','<<p.y<<','<<p.z<<')';}
void inspect(const Game& g,const char* label){
 const auto& p=g.pedestrians[0];const Vec3 eye=p.position+Vec3{0,1.6f,0},to=g.player+Vec3{0,1.1f,0},dir=normalized(to-eye);const float reach=length(to-eye);
 std::cout<<"\n"<<label<<" people="<<g.pedestrians.size()<<" vehicles="<<g.vehicles.size()<<" officer=";point(p.position);std::cout<<" yaw="<<p.yaw<<" worldBlocked="<<g.world.blocked(p.position,.35f)<<" ready="<<g.world.collisionReady(p.position)<<" player=";point(g.player);std::cout<<" reach="<<reach<<'\n';
 WorldLawSpace ws(g.world);GameLawSpace gs(g);gs.setObserver(1);Vec3 projected;
 std::cout<<"staticSight="<<ws.lineClear(eye,to)<<" fullSight="<<gs.lineClear(eye,to)<<" staticProject="<<ws.project(p.position,.35f,projected)<<" staticDirectWalk="<<ws.walkClear(p.position,g.player,.35f)<<" fullDirectWalk="<<gs.walkClear(p.position,g.player,.35f)<<" zeroMoveRecovery=";point(g.world.move(p.position,{},.35f));std::cout<<'\n';
 for(const auto& c:g.world.chunks)for(size_t n=0;n<c.solids.size();++n){const auto& b=c.solids[n];const float dx=p.position.x-clamp(p.position.x,b.min.x,b.max.x),dz=p.position.z-clamp(p.position.z,b.min.z,b.max.z);const float near=std::hypot(dx,dz);const float t=hitBox(eye,dir,b.min,b.max,reach);
  if(near<1.0f||t<reach-.05f){std::cout<<"box chunk="<<c.x<<','<<c.z<<" index="<<n<<" planarDistance="<<near<<" sightDistance="<<t<<" min=";point(b.min);std::cout<<" max=";point(b.max);std::cout<<'\n';}}
 for(size_t n=0;n<g.vehicles.size();++n){const auto& v=g.vehicles[n];const float distance=length(v.position-p.position),t=hitVehicle(eye,dir,v,reach),movement=hitVehicle(p.position,normalized(g.player-p.position),v,.08f,.35f,true);
  if(distance<8||t<reach-.05f||movement<.079f){std::cout<<"vehicle="<<n<<" kind="<<int(v.kind)<<" police="<<v.police<<" centerDistance="<<distance<<" sightDistance="<<t<<" stepDistance="<<movement<<" position=";point(v.position);std::cout<<" yaw="<<v.yaw<<'\n';}}
 for(size_t n=1;n<g.pedestrians.size();++n){const auto& other=g.pedestrians[n];if(other.health<=0)continue;const float distance=length(other.position-p.position),t=hitPerson(eye,dir,other,reach);
  if(distance<2||t<reach-.05f){std::cout<<"person="<<n<<" id="<<other.identity<<" distance="<<distance<<" sightDistance="<<t<<" position=";point(other.position);std::cout<<'\n';}}
 for(int direction=0;direction<8;++direction){const Vec3 candidate=p.position+forward(float(direction)*Pi/4)*.5f;std::cout<<"nearby=";point(candidate);std::cout<<" blocked="<<g.world.blocked(candidate,.35f)<<" project="<<ws.project(candidate,.35f,projected)<<" staticWalk="<<ws.walkClear(p.position,candidate,.35f)<<" fullWalk="<<gs.walkClear(p.position,candidate,.35f)<<'\n';}
 if(g.lawState().count){const auto& u=g.lawState().units[0];std::cout<<"law phase="<<int(u.phase)<<" memory="<<u.memory.valid<<" radio="<<u.radio.valid<<" goal="<<u.hasGoal<<" retry="<<u.pathRetry<<" aim="<<g.policePose(0).aim<<'\n';}
}
int main(){std::cout<<std::setprecision(9);Game g;g.initialize();g.player={-136,0,-86};g.player.y=g.world.height(g.player.x,g.player.z);g.world.stream(g.player);for(size_t n=0;n<4;++n){const auto& p=g.pedestrians[n];std::cout<<"spawn officer="<<n<<" position=";point(p.position);std::cout<<" blocked="<<g.world.blocked(p.position,.35f)<<" zeroMoveRecovery=";point(g.world.move(p.position,{},.35f));std::cout<<'\n';}g.yaw=0;g.pitch=-.8f;g.dayTime=14;inspect(g,"initial");Input fire;fire.aim=fire.fire=true;g.update(fire,1.f/60,false);for(int i=0;i<120;++i){g.update({},1.f/60,false);if(i==29||i==119)inspect(g,i==29?"0.5 seconds":"2 seconds");}}
