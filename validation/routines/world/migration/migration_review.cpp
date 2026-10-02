#include "/workspace/GTA-6-ChatGPT-v0.5-routines/src/world.h"
#include "/workspace/GTA-6-ChatGPT-v0.5-routines/src/pedestrians.h"
#define private public
#include "/workspace/GTA-6-ChatGPT-v0.5-routines/src/game.h"
#undef private
#include <cassert>
#include <cstdio>
#include <fstream>
#include <set>
#include <iterator>
using namespace mc;
using Bytes=std::vector<unsigned char>;
Bytes read(const char* name){std::ifstream f(name,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
void write(const char* name,const Bytes& b){std::ofstream f(name,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));}
uint32_t get(const Bytes& b,size_t at){assert(at+4<=b.size());uint32_t result=0;for(unsigned k=0;k<4;++k)result|=uint32_t(b[at+k])<<(k*8);return result;}
void put(Bytes& b,size_t at,uint32_t value){for(unsigned k=0;k<4;++k)b[at+k]=static_cast<unsigned char>(value>>(k*8));}
Bytes legacy(Bytes b,int version){
 const size_t vehicles=get(b,112),peopleOffset=116+vehicles*76,people=get(b,peopleOffset);
 assert(vehicles==0);b.resize(peopleOffset+4+people*28+16);
 if(version<=2)b.resize(b.size()-16);else if(version==3)b.resize(b.size()-12);
 put(b,8,uint32_t(version));put(b,12,uint32_t(b.size()-20));uint32_t crc=~0u;
 for(size_t i=20;i<b.size();++i){crc^=b[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}put(b,16,~crc);return b;
}
bool equalOld(const Pedestrian& a,const Pedestrian& b){return length(a.position-b.position)<.00001f&&a.yaw==b.yaw&&a.phase==b.phase&&a.panic==b.panic&&a.health==b.health;}
const PedestrianPlace* place(uint32_t id){for(const auto& p:World::pedestrianPlaces())if(p.id==id)return &p;return nullptr;}
int main(){
 for(int mode=0;mode<5;++mode)for(int version=1;version<=4;++version){
  Game original;original.world.stream({8,0,8});original.money=7654;original.vehicles.clear();
  original.player=mode==1?Vec3{4096,0,2048}:mode==4?Vec3{385,0,8}:Vec3{8,0,8};
  original.world.stream(original.player);const int count=mode==3?4:84;
  for(int i=0;i<count;++i){
   Pedestrian p;int block=std::max(0,i-4);float x=float((block%5)-2)*128+12,z=float(((block/5)%5)-2)*128+12,walk=float((block*31)%100);
   if(block&1)x+=walk;else z+=walk;
   if(i<4){x=float(i)*8+12;z=12;}
   if(mode==1){x+=4096;z+=2048;}
   p.position={x,original.world.height(x,z),z};p.yaw=float(i%10)*.1f;p.phase=float(i)*.17f;p.panic=float(i%3)*.5f;p.health=mode==2?0.0f:(i%9==0?0.0f:float(100-i%31));original.pedestrians.push_back(p);
  }
  const auto before=original.pedestrians;assert(original.save("/tmp/migration-current.save"));write("/tmp/migration-legacy.save",legacy(read("/tmp/migration-current.save"),version));
  Game restored;assert(restored.load("/tmp/migration-legacy.save"));assert(restored.money==7654);
  std::set<uint32_t> ids,homes,works;size_t reused=0,created=0;
  for(size_t i=0;i<restored.pedestrians.size();++i){const auto& p=restored.pedestrians[i];const auto& b=restored.pedestrianSimulation.brains[i];
   assert(p.identity&&ids.insert(p.identity).second&&p.identity<restored.pedestrianSimulation.nextIdentity);
   if(p.identity<=before.size())assert(equalOld(p,before[p.identity-1]));
   else {++created;assert(b.persistent&&length(p.position-place(b.home)->position)<.00001f);}
   if(b.persistent){assert(i>=4&&i<20);assert(place(b.home)&&place(b.home)->kind==PedestrianPlaceKind::Home&&homes.insert(b.home).second);assert(place(b.work)&&place(b.work)->kind==PedestrianPlaceKind::Work&&works.insert(b.work).second);if(p.identity<=before.size()){++reused;assert(p.health>0);}}
  }
  for(uint32_t i=1;i<=before.size();++i)assert(ids.contains(i));
  assert(homes.size()==16&&works.size()==16&&reused+created==16&&restored.pedestrians.size()==before.size()+created&&restored.pedestrians.size()<=100);
  if(mode==1||mode==2||mode==3)assert(created==16);
  for(size_t i=0;i<4;++i)assert(restored.pedestrians[i].identity==i+1&&equalOld(restored.pedestrians[i],before[i]));
  assert(restored.save("/tmp/migration-v5-a.save"));Game again;assert(again.load("/tmp/migration-v5-a.save"));assert(again.save("/tmp/migration-v5-b.save"));assert(read("/tmp/migration-v5-a.save")==read("/tmp/migration-v5-b.save"));
  if(mode==1){const auto frozen=again.pedestrians;for(int f=0;f<10;++f)again.update(Input{},1.f/60,false);for(size_t i=4;i<20;++i)assert(length(again.pedestrians[i].position-frozen[i].position)<.00001f);again.player={8,0,8};again.world.stream(again.player);again.update(Input{},1.f/60,false);for(size_t i=4;i<20;++i)assert(again.pedestrians[i].identity==frozen[i].identity);}
  std::printf("mode=%d version=%d old=%zu preserved=%zu reused=%zu new=%zu total=%zu exact_v5_roundtrip=1\n",mode,version,before.size(),before.size(),reused,created,restored.pedestrians.size());
 }
 Game cap;cap.world.stream({8,0,8});cap.pedestrians.resize(85);assert(cap.save("/tmp/migration-cap-current.save"));write("/tmp/migration-cap-legacy.save",legacy(read("/tmp/migration-cap-current.save"),4));Game refused;assert(!refused.load("/tmp/migration-cap-legacy.save"));cap.pedestrians.resize(101);assert(!cap.save("/tmp/migration-over-cap.save"));
 std::puts("legacy85_rejected=1 v5_save101_rejected=1 all_migration_checks_passed=1");
}
