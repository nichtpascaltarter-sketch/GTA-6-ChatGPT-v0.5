#include "src/world_streamer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
using Clock=std::chrono::steady_clock;
using Key=std::pair<int,int>;
std::pair<size_t,size_t> memoryKiB(){
    std::ifstream file("/proc/self/status");std::string line;size_t rss=0,hwm=0;
    while(std::getline(file,line)){std::istringstream in(line);std::string key;size_t value;in>>key>>value;if(key=="VmRSS:")rss=value;if(key=="VmHWM:")hwm=value;}
    return {rss,hwm};
}
Key key(mc::Vec3 p){return {int(std::floor(p.x/mc::World::ChunkSize)),int(std::floor(p.z/mc::World::ChunkSize))};}
std::vector<mc::Vec3> route(const std::string& name){
    std::vector<mc::Vec3> p;
    if(name=="city_axial"||name=="city_diagonal"){
        for(int x=-8;x<=8;++x)p.push_back({x*128.f+64,20,name=="city_axial"?64:x*128.f+64});
    }else if(name=="coastal_aircraft"){
        mc::World height;
        for(int i=0;i<=5000;++i){mc::Vec3 q=mc::World::coastalRoadPoint(float(i)/5000);q.y=height.height(q.x,q.z)+160;
            if(p.empty()||key(p.back())!=key(q))p.push_back(q);}
    }
    return p;
}
double ms(Clock::duration duration){return std::chrono::duration<double,std::milli>(duration).count();}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string name=argv[1];const auto points=route(name);if(points.empty())return 2;
    std::cout<<"route,pass,sample,x,y,z,cx,cz,new_tiles,retained_tiles,service_wall_ms,max_pair_ms,max_call_ms,publish_latency_ms,poll_pairs,vertices,indices,resident_capacity_bytes,new_capacity_bytes,peak_completed_bytes,peak_staged_bytes,peak_queued,peak_inflight,peak_completed,fallbacks,publications,scheduled,rejected,rss_kib,hwm_kib\n";
    std::cout<<std::fixed<<std::setprecision(6);
    mc::World world;const auto coldStart=Clock::now();world.stream(points.front());const auto coldEnd=Clock::now();
    mc::WorldStreamer service;std::string error;size_t sample=0;uint64_t checksum=0;mc::Vec3 previous=points.front();
    auto measure=[&](mc::Vec3 position,int pass){
        if(key(previous)==key(position))return;
        std::set<Key> before;for(const auto& c:world.chunks)before.emplace(c.x,c.z);
        const auto beforeStats=service.stats();const uint64_t revision=world.revision;
        double cpuMs=0,maxPair=0,maxCall=0;size_t polls=0,peakCompletedBytes=0,peakStagedBytes=0,peakQueued=0,peakInflight=0,peakCompleted=0;
        const auto start=Clock::now();auto nextFrame=start;
        auto capture=[&]{
            const auto stats=service.stats();peakCompletedBytes=std::max(peakCompletedBytes,stats.completedBytes);peakStagedBytes=std::max(peakStagedBytes,stats.stagedBytes);
            peakQueued=std::max(peakQueued,stats.queued);peakInflight=std::max(peakInflight,stats.inFlight);peakCompleted=std::max(peakCompleted,stats.completed);
            if(stats.queued>8||stats.inFlight>2||stats.completed>4||stats.completedBytes>mc::WorldStreamer::MaxCompletedBytes)throw std::runtime_error("queue invariant violation");
        };
        auto call=[&](mc::Vec3 p){
            capture();const auto callStart=Clock::now();if(!service.update(world,p,1,error))throw std::runtime_error(error);const auto callEnd=Clock::now();
            const double elapsed=ms(callEnd-callStart);cpuMs+=elapsed;maxCall=std::max(maxCall,elapsed);capture();
            if(world.chunks.size()!=49||!world.collisionReady(p))throw std::runtime_error("world invariant violation");
        };
        while(world.revision==revision){
            const double pairBefore=cpuMs;
            call(polls?position:previous);call(position);maxPair=std::max(maxPair,cpuMs-pairBefore);++polls;
            if(world.revision!=revision)break;
            if(polls>180)throw std::runtime_error("publication exceeded 3 second deadline");
            nextFrame+=std::chrono::nanoseconds(16666667);std::this_thread::sleep_until(nextFrame);
        }
        const auto published=Clock::now();const auto stats=service.stats();
        if(stats.synchronousFallbacks!=beforeStats.synchronousFallbacks||stats.publications-beforeStats.publications!=1)throw std::runtime_error("unexpected fallback or publication count");
        size_t added=0,capacity=0,newCapacity=0,vertices=0,indices=0;
        for(const auto& chunk:world.chunks){const bool isNew=!before.count({chunk.x,chunk.z});added+=isNew;capacity+=mc::World::chunkBytes(chunk);if(isNew)newCapacity+=mc::World::chunkBytes(chunk);vertices+=chunk.mesh.vertices.size();indices+=chunk.mesh.indices.size();}
        checksum+=vertices+indices;const auto mem=memoryKiB();const Key center=key(position);
        std::cout<<name<<','<<pass<<','<<sample++<<','<<position.x<<','<<position.y<<','<<position.z<<','<<center.first<<','<<center.second<<','<<added<<','<<world.chunks.size()-added<<','<<cpuMs<<','<<maxPair<<','<<maxCall<<','<<ms(published-start)<<','<<polls<<','<<vertices<<','<<indices<<','<<capacity<<','<<newCapacity<<','<<peakCompletedBytes<<','<<peakStagedBytes<<','<<peakQueued<<','<<peakInflight<<','<<peakCompleted<<','<<stats.synchronousFallbacks-beforeStats.synchronousFallbacks<<','<<stats.publications-beforeStats.publications<<','<<stats.scheduled-beforeStats.scheduled<<','<<stats.rejectedResults-beforeStats.rejectedResults<<','<<mem.first<<','<<mem.second<<'\n';
        previous=position;
    };
    for(int pass=1;pass<=3;++pass){for(size_t i=1;i<points.size();++i)measure(points[i],pass);for(size_t i=points.size()-1;i>0;--i)measure(points[i-1],pass);}
    const auto idleStart=Clock::now();for(int i=0;i<100000;++i){if(!service.update(world,points.front(),1,error)||!service.update(world,points.front(),1,error))return 4;}const auto idleEnd=Clock::now();
    const auto stats=service.stats();
    std::cerr<<"route="<<name<<" samples="<<sample<<" checksum="<<checksum<<" cold_load_ms="<<ms(coldEnd-coldStart)<<" idle_pairs=100000 idle_total_ms="<<ms(idleEnd-idleStart)<<" scheduled="<<stats.scheduled<<" built="<<stats.built<<" installed="<<stats.installed<<" publications="<<stats.publications<<" fallbacks="<<stats.synchronousFallbacks<<'\n';
}
