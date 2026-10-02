#include "world.h"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <map>
#include <cassert>
#include <cstdio>
#include "arena_under_test.h"

int main(){
 mc::World w;ArenaAllocator va,ia;va.reset(64*1024*1024/40);ia.reset(16*1024*1024/4);
 struct Entry{ArenaRange v,i;};std::map<std::pair<int,int>,Entry> residents;
 const mc::Vec3 route[]={{8,0,8},{136,0,8},{264,0,8},{392,0,136},{8,0,8},{2510,0,260},{8,0,8},{8,0,8}};
 for(unsigned phase=0;phase<8;++phase){w.stream(route[phase]);std::map<std::pair<int,int>,Entry> next;size_t added=0,retained=0;
  auto plannedV=va,plannedI=ia;
  for(const auto& c:w.chunks){auto key=std::make_pair(c.x,c.z);auto old=residents.find(key);
   if(phase!=7&&old!=residents.end()){next[key]=old->second;++retained;continue;}
   Entry e;bool fit=plannedV.allocate((uint32_t)c.mesh.vertices.size(),e.v)&&plannedI.allocate((uint32_t)c.mesh.indices.size(),e.i);
   if(!fit){std::printf("phase%u FAILED first=%d,%d added=%zu\n",phase,c.x,c.z,added);return 1;}
   next[key]=e;++added;
  }
  va=std::move(plannedV);ia=std::move(plannedI);
  for(const auto& [key,e]:residents)if(phase==7||next.find(key)==next.end()){va.release(e.v);ia.release(e.i);}
  residents=std::move(next);std::printf("phase%u added=%zu retained=%zu no pressure/repack\n",phase,added,retained);
 }
}
