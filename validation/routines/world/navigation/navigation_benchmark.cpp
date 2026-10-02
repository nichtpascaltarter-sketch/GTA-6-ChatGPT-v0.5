#include "/workspace/GTA-6-ChatGPT-v0.5-routines/src/world.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
int main(){mc::World world;world.stream({8,0,8});std::vector<double> times;size_t bytes=0;for(int i=0;i<32;++i){const auto start=std::chrono::steady_clock::now();const auto graph=world.pedestrianNetwork({8,0,8});const auto end=std::chrono::steady_clock::now();times.push_back(std::chrono::duration<double,std::milli>(end-start).count());bytes=graph.nodes.capacity()*sizeof(mc::PedestrianNode)+graph.edges.capacity()*sizeof(mc::PedestrianEdge)+graph.places.capacity()*sizeof(mc::PedestrianPlace);}std::sort(times.begin(),times.end());std::printf("median_ms=%.3f p95_ms=%.3f max_ms=%.3f graph_capacity_bytes=%zu\n",times[16],times[30],times[31],bytes);}
