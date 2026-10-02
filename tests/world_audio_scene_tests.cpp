#include "../src/audio_scene.h"
#include "../src/world_synth.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
std::atomic<bool> countAllocations{false};
std::atomic<unsigned> allocations{0};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool near(float a,float b,float tolerance=.0001f){return std::abs(a-b)<tolerance;}
constexpr float Step=.05f;
constexpr mc::Vec3 Listener{12,1.6f,12},Ahead{0,0,1};
mc::Pedestrian walker(const mc::World& world,uint32_t id,float x=14,float z=12){
    mc::Pedestrian p;p.identity=id;p.position={x,world.height(x,z),z};p.phase=mc::Pi*.5f-.12f;p.motion=.6f;return p;
}
mc::Vehicle engine(const mc::World& world,uint64_t id,float x=14,float z=12){
    mc::Vehicle v;v.identity=id;v.position={x,world.height(x,z),z};v.speed=8;v.throttle=.3f;return v;
}
void move(mc::Game& game,float distance=.08f){
    auto& p=game.pedestrians[0];p.position.z+=distance;p.position.y=game.world.height(p.position.x,p.position.z);p.phase+=distance*3;
}
mc::WorldAudioState sample(mc::WorldAudioScene& scene,const mc::Game& game,bool advance=true,float dt=Step,uint64_t epoch=1){
    return scene.update(game,Listener,Ahead,dt,advance,epoch);
}
void setup(mc::Game& game){game.world.stream(Listener);}

void contactsAndSuppression(){
    mc::Game game;setup(game);game.pedestrians.push_back(walker(game.world,7));mc::WorldAudioScene scene;
    auto first=sample(scene,game);require(first.footCount==1&&first.feet[0].strikeSerial==0,"first admission replayed a foot contact");
    const auto id=first.feet[0].id;move(game);auto landed=sample(scene,game);
    require(landed.footCount==1&&landed.feet[0].id==id&&landed.feet[0].strikeSerial==1,"real displacement/landing failed");
    require(landed.feet[0].side==0&&near(landed.feet[0].strikeAgeSeconds,.025f,.00001f),"left landing interpolation failed");
    const auto contacts=scene.stats().contacts;
    for(int i=0;i<10;++i)sample(scene,game);
    require(scene.stats().contacts==contacts,"stationary walker replayed a contact");
    game.pedestrians[0].phase+=.2f;sample(scene,game);
    require(scene.stats().contacts==contacts,"phase-only pose change produced contact");
    bool right=false;
    for(int i=0;i<30;++i){move(game);const auto frame=sample(scene,game);if(frame.footCount&&frame.feet[0].strikeSerial>1&&frame.feet[0].side==1)right=true;}
    require(right,"alternating actual gait never produced right landing");
    auto before=scene.stats().contacts;sample(scene,game,false,0);move(game);sample(scene,game);
    require(scene.stats().contacts==before,"focus resume replayed contact");
    game.paused=true;for(int i=0;i<20;++i){move(game);sample(scene,game);}require(scene.stats().contacts==before,"paused motion produced contact");game.paused=false;
    game.pedestrians[0].sitBlend=1;for(int i=0;i<20;++i){move(game);require(sample(scene,game).footCount==0,"seated actor admitted foot source");}
    require(scene.stats().contacts==before,"seated movement produced contact");game.pedestrians[0].sitBlend=0;
    game.pedestrians[0].position.y+=2;sample(scene,game);game.pedestrians[0].position.z+=.08f;game.pedestrians[0].phase+=.24f;
    require(sample(scene,game).footCount==0&&scene.stats().contacts==before,"airborne actor produced foot contact");
    game.pedestrians[0].health=0;require(sample(scene,game).footCount==0,"dead actor admitted");
    game.pedestrians[0]=walker(game.world,9);game.pedestrians[0].phase=mc::Pi*.5f-.001f;scene.reset(1);sample(scene,game);
    const auto beforeFast=scene.stats().contacts;move(game,.001f);sample(scene,game,true,.0001f);
    require(scene.stats().contacts==beforeFast,"implausible displacement at tiny delta produced contact");
    move(game);require(!sample(scene,game,true,std::numeric_limits<float>::quiet_NaN()).advancing,"invalid simulation delta advanced contacts");
    std::cout<<"PASS actual displacement, alternating landings, interpolated age, blocked/pause/seat/air/death suppression\n";
}

void lifecycleAndResidency(){
    mc::Game game;setup(game);game.vehicles.push_back(engine(game.world,40));game.pedestrians.push_back(walker(game.world,7));mc::WorldAudioScene scene;
    auto first=sample(scene,game);const auto engineId=first.engines[0].id,footId=first.feet[0].id,epoch=first.epoch;
    auto idle=sample(scene,game,false,0);require(idle.engines[0].id==engineId&&!idle.advancing&&idle.publicationSerial>first.publicationSerial,"stationary heartbeat renewed engine or failed publication");
    move(game);sample(scene,game);require(scene.stats().contacts==0,"suspend/resume did not prime");
    game.vehicles[0].position.z+=20;game.pedestrians[0].position.z+=4;auto jumped=sample(scene,game);
    require(jumped.engines[0].id!=engineId&&scene.stats().sourceRenewals==2,"entity discontinuity failed generation renewal");
    move(game);auto resumed=sample(scene,game);require(resumed.footCount&&resumed.feet[0].id!=footId,"foot teleport retained source generation");
    const auto afterJumpEngine=jumped.engines[0].id;
    game.world.chunks.clear();auto absent=sample(scene,game);require(absent.engineCount==0&&absent.footCount==0,"nonresident sources survived");
    game.world.stream({3000,0,2000});game.world.stream(Listener);auto restored=sample(scene,game);
    require(restored.engineCount&&restored.engines[0].id!=afterJumpEngine,"residency return retained stale source ID");
    auto loaded=sample(scene,game,true,Step,2);require(loaded.epoch!=epoch&&loaded.feet[0].strikeSerial==0,"load epoch replayed stale gait");
    auto cut=scene.update(game,{80,2,12},Ahead,Step,true,2);require(cut.epoch!=loaded.epoch,"listener camera cut kept stale audio epoch");
    auto invalid=scene.update(game,{std::numeric_limits<float>::quiet_NaN(),0,0},Ahead,Step,true,2);
    require(!invalid.advancing&&invalid.engineCount==0&&scene.stats().trackedFeet==0,"invalid listener did not fail closed");
    std::cout<<"PASS heartbeat, source teleport, residency loss, load epoch, camera cut and malformed listener\n";
}

void selectionPanAndEligibility(){
    mc::Game game;setup(game);mc::WorldAudioScene scene;
    for(unsigned i=0;i<12;++i){game.vehicles.push_back(engine(game.world,100+i,14+float(i),12));game.pedestrians.push_back(walker(game.world,200+i,14+float(i),12));}
    const auto first=sample(scene,game);require(first.engineCount==8&&first.footCount==8,"nearest source limits failed");
    for(unsigned i=1;i<8;++i)require(first.engines[i].right<first.engines[i-1].right,"engine selection was not nearest first");
    std::reverse(game.vehicles.begin(),game.vehicles.end());std::reverse(game.pedestrians.begin(),game.pedestrians.end());
    for(auto& p:game.pedestrians){p.position.z+=.02f;p.phase+=.06f;}
    const auto reordered=sample(scene,game);
    for(unsigned i=0;i<8;++i){require(reordered.engines[i].id==first.engines[i].id,"vehicle vector reorder changed source identity");require(reordered.feet[i].id==first.feet[i].id,"person reorder changed source identity");}
    game.vehicles.clear();game.pedestrians.clear();game.vehicles.push_back(engine(game.world,500,22,12));scene.reset(1);
    const auto right=sample(scene,game).engines[0];game.vehicles[0].position.x=2;
    const auto left=sample(scene,game).engines[0];require(right.left<.00001f&&left.right<.00001f&&near(right.right,left.left),"equal-power left/right pan mismatch");
    game.vehicles[0].position={12,Listener.y,12};const auto center=sample(scene,game).engines[0];require(near(center.left,center.right),"coincident source pan was not centered");
    const auto vertical=scene.update(game,Listener,{0,1,0},Step,true,1);require(near(vertical.engines[0].left,center.left),"vertical listener direction corrupted pan");
    game.vehicles[0].position={22,0,12};
    const float huge=std::numeric_limits<float>::max();
    const auto overflow=scene.update(game,Listener,{huge,0,huge},Step,true,1);
    require(overflow.engines[0].left<.00001f&&std::isfinite(overflow.engines[0].right),"overflowing listener direction lost prior pan");
    game.vehicles[0].position={12+mc::WorldAudioScene::EngineRadius,Listener.y,12};require(sample(scene,game).engineCount==0,"engine cutoff leaked source");
    game.vehicles[0].position.x-=.01f;auto edge=sample(scene,game);require(edge.engineCount==1&&edge.engines[0].right<.000001f,"engine cutoff was discontinuous");
    game.vehicles[0].position={14,0,12};game.occupied=0;require(sample(scene,game).engineCount==0,"occupied engine doubled centered player sound");game.occupied=-1;
    game.vehicles[0].parked=true;require(sample(scene,game).engineCount==0,"parked engine admitted");game.vehicles[0].parked=false;game.vehicles[0].speed=0;
    require(sample(scene,game,false,0).engineCount==1,"healthy stationary engine lost idle sound");
    for(int kind=0;kind<4;++kind){game.vehicles[0].kind=mc::VehicleKind(kind);require(sample(scene,game).engines[0].kind==kind,"vehicle engine kind lost");}
    game.vehicles[0].health=0;require(sample(scene,game).engineCount==0,"dead vehicle engine admitted");game.vehicles[0].health=100;
    game.vehicles[0].speed=std::numeric_limits<float>::infinity();require(sample(scene,game).engineCount==0,"malformed engine admitted");
    std::cout<<"PASS nearest eight, reorder identity, equal-power pan, cutoff, occupied/parked/dead filtering and four kinds\n";
}

void selectionTurnoverDsp(){
    mc::Game game;setup(game);mc::WorldAudioScene scene;mc::WorldSynth mixer;
    for(unsigned i=0;i<9;++i){auto p=walker(game.world,i+1,i<7?12.1f+float(i)*.1f:14.0001f+float(i-7)*.0001f);p.phase=mc::Pi*.5f-.5f;game.pedestrians.push_back(p);}
    mixer.update(sample(scene,game));
    auto advance=[&](){for(auto& p:game.pedestrians){p.position.z+=.1f;p.phase+=.3f;}mixer.update(sample(scene,game));};
    advance();game.pedestrians[8].position.x=14;advance();
    require(mixer.stats().strikes==7,"new nearest source replayed its admission strike");
    float tail[96]{};mixer.render(tail,48);
    game.pedestrians[8].position.x=14.0003f;advance();
    require(mixer.stats().strikes==7,"retained-tail readmission replayed old strike");
    for(int i=0;i<11;++i)advance();
    require(mixer.stats().strikes>7&&mixer.stats().rejectedSources==0,"readmitted walkers did not resume valid future contacts");
    game.pedestrians[0].identity=0;sample(scene,game);
    require(scene.stats().trackedFeet==8,"identity-zero pedestrian was admitted");
    std::cout<<"PASS near-tie nearest-source turnover, DSP admission and retained-tail readmission without replay\n";
}

void surfacesAndBounds(){
    mc::Game game;game.world.stream({2674,.4f,768});game.pedestrians.push_back(walker(game.world,1,2674,768));mc::WorldAudioScene scene;
    const mc::Vec3 eye=game.pedestrians[0].position+mc::Vec3{2,1.6f,0};scene.update(game,eye,Ahead,Step,true,1);move(game);
    auto contact=scene.update(game,eye,Ahead,Step,true,1);require(contact.footCount==1&&contact.feet[0].strikeSerial==1&&contact.feet[0].surface==mc::FootSurface::Wood,"wooden pier over water lost dry foot contact");
    game.world.stream({3000,mc::World::WaterLevel,900});game.pedestrians[0]=walker(game.world,1,3000,900);game.pedestrians[0].position.y=mc::World::WaterLevel-1.1f;
    require(scene.update(game,{3000,2,900},Ahead,Step,true,1).footCount==0,"submerged foot admitted");
    game.world.stream(Listener);game.pedestrians.clear();game.vehicles.clear();scene.reset(1);
    for(unsigned i=0;i<260;++i)game.vehicles.push_back(engine(game.world,i+1,12+float(i%10),12));
    for(unsigned i=0;i<132;++i)game.pedestrians.push_back(walker(game.world,i+1,12+float(i%10),12));
    sample(scene,game);require(scene.stats().trackedEngines==256&&scene.stats().trackedFeet==128&&scene.stats().capacityDrops==8,"history bounds did not reject oversized state");
    game.vehicles.resize(2);game.pedestrians.resize(2);game.vehicles[1].identity=game.vehicles[0].identity;game.pedestrians[1].identity=game.pedestrians[0].identity;
    sample(scene,game);require(scene.stats().duplicateIdentities==2&&scene.stats().trackedEngines==1&&scene.stats().trackedFeet==1,"duplicate identities created conflicting source histories");
    allocations=0;countAllocations=true;
    for(int i=0;i<1000;++i)sample(scene,game);
    countAllocations=false;require(allocations==0,"tracker allocated in its hot path");
    std::cout<<"PASS pier contact, swimming suppression, bounded capacity, duplicate rejection and 1000 allocation-free updates ("<<sizeof(scene)<<" bytes tracker)\n";
}

std::vector<char> bytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void vehicleRuntimeIdentities(){
    mc::Game game;game.initialize();
    std::vector<uint64_t> ids;for(const auto& vehicle:game.vehicles){require(vehicle.identity!=0,"initialized vehicle lacked identity");ids.push_back(vehicle.identity);}
    auto sorted=ids;std::sort(sorted.begin(),sorted.end());require(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"initialized duplicate vehicle identity");
    game.update({},Step,false);for(size_t i=0;i<ids.size();++i)require(game.vehicles[i].identity==ids[i],"ordinary update renewed vehicle identity");
    const auto path=std::filesystem::temp_directory_path()/"meridian-world-audio-identity-test.mcs";
    require(game.save(path.string()),"identity baseline save failed");const auto original=bytes(path);
    for(auto& vehicle:game.vehicles)vehicle.identity+=100000;
    require(game.save(path.string())&&bytes(path)==original,"runtime identity changed save bytes");
    mc::Game loaded;require(loaded.load(path.string()),"identity save reload failed");std::filesystem::remove(path);
    for(size_t i=0;i<loaded.vehicles.size();++i)require(loaded.vehicles[i].identity&&loaded.vehicles[i].identity!=game.vehicles[i].identity,"load did not assign runtime-only identities");
    loaded.vehicles.push_back(loaded.vehicles[0]);loaded.update({},Step,false);
    require(loaded.vehicles.back().identity!=loaded.vehicles[0].identity,"copied vehicle retained duplicate runtime identity");
    loaded.vehicles.pop_back();
    auto& recycled=loaded.vehicles[12];const auto recycledId=recycled.identity;recycled.position={1700,0,1000};
    for(int i=0;i<22;++i)loaded.update({},Step,false);
    require(loaded.vehicles[12].identity!=recycledId&&loaded.vehicles[12].identity!=0,"traffic recycle retained stale identity");
    loaded.player=mc::Game::harborSplitContact();loaded.player.y=loaded.world.height(loaded.player.x,loaded.player.z);loaded.world.stream(loaded.player);
    size_t bike=0;while(bike<loaded.vehicles.size()&&loaded.vehicles[bike].kind!=mc::VehicleKind::Motorcycle)++bike;
    require(bike<loaded.vehicles.size(),"starter motorcycle absent");const auto loanId=loaded.vehicles[bike].identity;
    mc::Input input;input.mission=true;loaded.update(input,Step,false);
    require(loaded.harborSplit.phase==mc::TrialPhase::Boarding&&loaded.vehicles[bike].identity!=loanId&&loaded.vehicles[bike].identity!=0,"trial replacement did not renew identity");
    std::cout<<"PASS vehicle initialization, stable update, byte-identical saves, fresh load, traffic recycle and trial replacement\n";
}
}
void* operator new(std::size_t bytes){if(countAllocations)++allocations;if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main(){try{contactsAndSuppression();lifecycleAndResidency();selectionPanAndEligibility();selectionTurnoverDsp();surfacesAndBounds();vehicleRuntimeIdentities();return 0;}catch(const std::exception& error){countAllocations=false;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
