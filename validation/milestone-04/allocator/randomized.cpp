#include <algorithm>
#include <cstdint>
#include <vector>
#include <random>
#include <cassert>
#include <cstdio>
#include "arena_under_test.h"

int main(){
 std::mt19937 rng(752994); constexpr uint32_t capacity=8192;
 for(int trial=0;trial<40;++trial){
  ArenaAllocator allocator;allocator.reset(capacity);std::vector<ArenaRange> live;
  for(int step=0;step<5000;++step){
   if(!live.empty()&&(rng()%3==0)){size_t i=rng()%live.size();allocator.release(live[i]);live.erase(live.begin()+i);}
   else {ArenaRange r;if(allocator.allocate(rng()%100,r))live.push_back(r);}
   std::vector<uint8_t> occupied(capacity);
   uint64_t total=0;
   for(auto r:live){assert(uint64_t(r.first)+r.count<=capacity);total+=r.count;for(uint32_t j=0;j<r.count;++j)assert(occupied[r.first+j]++==0);}
   uint32_t end=0;bool first=true;
   for(auto r:allocator.free){assert(r.count);assert(first||end<r.first);first=false;end=r.first+r.count;assert(end<=capacity);total+=r.count;for(uint32_t j=0;j<r.count;++j)assert(occupied[r.first+j]++==0);}
   assert(total==capacity);for(auto n:occupied)assert(n==1);
  }
  for(auto r:live)allocator.release(r);assert(allocator.free.size()==1&&allocator.free[0].first==0&&allocator.free[0].count==capacity);
 }
 std::puts("Actual arena allocator: 200000 randomized allocate/release steps passed nonoverlap, accounting and coalescing checks.");
}
