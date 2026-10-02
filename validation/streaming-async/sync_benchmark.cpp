#include "src/world.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
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
        // Dense position samples on the authored coastal centerline; only new chunk centers enter the benchmark.
        mc::World height;
        for(int i=0;i<=5000;++i){mc::Vec3 q=mc::World::coastalRoadPoint(float(i)/5000);q.y=height.height(q.x,q.z)+160;
            if(p.empty()||key(p.back())!=key(q))p.push_back(q);}
    }
    return p;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string name=argv[1];const auto points=route(name);if(points.empty())return 2;
    std::cout<<"route,pass,sample,kind,x,y,z,cx,cz,new_tiles,retained_tiles,stream_ms,combined_ms,total_ms,vertices,indices,chunk_used_bytes,chunk_capacity_bytes,combined_bytes,rss_kib,hwm_kib\n";
    std::cout<<std::fixed<<std::setprecision(6);
    mc::World world;size_t sample=0;uint64_t checksum=0;
    auto measure=[&](mc::Vec3 position,int pass,const char* kind){
        std::set<Key> before;for(const auto& c:world.chunks)before.emplace(c.x,c.z);
        const auto start=Clock::now();const bool changed=world.stream(position);const auto streamed=Clock::now();
        if(!changed)return false;
        const auto combineStart=Clock::now();mc::Mesh combined=world.combinedMesh();const auto done=Clock::now();
        size_t added=0,used=0,capacity=0;
        for(const auto& c:world.chunks){added+=!before.count({c.x,c.z});
            used+=c.mesh.vertices.size()*sizeof(mc::Vertex)+c.mesh.indices.size()*sizeof(uint32_t)+c.solids.size()*sizeof(mc::Box)+c.lights.size()*sizeof(mc::Light);
            capacity+=c.mesh.vertices.capacity()*sizeof(mc::Vertex)+c.mesh.indices.capacity()*sizeof(uint32_t)+c.solids.capacity()*sizeof(mc::Box)+c.lights.capacity()*sizeof(mc::Light);}
        const size_t bytes=combined.vertices.capacity()*sizeof(mc::Vertex)+combined.indices.capacity()*sizeof(uint32_t);const auto mem=memoryKiB();
        const double streamMs=std::chrono::duration<double,std::milli>(streamed-start).count(),combineMs=std::chrono::duration<double,std::milli>(done-combineStart).count();
        checksum+=combined.vertices.size()+combined.indices.size();if(!combined.indices.empty())checksum+=combined.indices.back();
        const Key center=key(position);
        std::cout<<name<<','<<pass<<','<<sample++<<','<<kind<<','<<position.x<<','<<position.y<<','<<position.z<<','<<center.first<<','<<center.second<<','<<added<<','<<world.chunks.size()-added<<','<<streamMs<<','<<combineMs<<','<<streamMs+combineMs<<','<<combined.vertices.size()<<','<<combined.indices.size()<<','<<used<<','<<capacity<<','<<bytes<<','<<mem.first<<','<<mem.second<<'\n';
        return true;
    };
    measure(points.front(),0,"cold");
    for(int pass=1;pass<=3;++pass){for(size_t i=1;i<points.size();++i)measure(points[i],pass,"boundary");for(size_t i=points.size()-1;i>0;--i)measure(points[i-1],pass,"boundary");}
    const auto start=Clock::now();for(int i=0;i<1000000;++i)checksum+=world.stream(points.front());const auto stop=Clock::now();
    std::cerr<<"route="<<name<<" samples="<<sample<<" checksum="<<checksum<<" unchanged_calls=1000000 unchanged_total_ms="<<std::chrono::duration<double,std::milli>(stop-start).count()<<'\n';
}
