#include "game.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

int main(){
    using Clock=std::chrono::steady_clock;
    for(int scenario=0;scenario<4;++scenario){
        mc::Game game;game.initialize();game.dayTime=scenario==1?12.f:scenario==2?18.f:8.f;
        game.pitch=-.8f;
        const auto initial=Clock::now();game.update({},1.f/60,false);
        const double firstUs=std::chrono::duration<double,std::micro>(Clock::now()-initial).count();
        for(int frame=0;frame<120;++frame)game.update({},1.f/60,false);
        std::vector<double> samples;samples.reserve(3600);
        mc::PedestrianStats peak{};
        for(int frame=0;frame<3600;++frame){
            mc::Input input;if(scenario==3&&frame%300==0)input.aim=input.fire=true;
            const auto start=Clock::now();game.update(input,1.f/60,false);
            samples.push_back(std::chrono::duration<double,std::micro>(Clock::now()-start).count());
            const auto stats=game.pedestrianStats();
            if(stats.decisions>8||stats.routeSearches>2||stats.routeExpansions>512||stats.sightChecks>8)return 1;
            peak.decisions=std::max(peak.decisions,stats.decisions);
            peak.routeSearches=std::max(peak.routeSearches,stats.routeSearches);
            peak.routeExpansions=std::max(peak.routeExpansions,stats.routeExpansions);
            peak.sightChecks=std::max(peak.sightChecks,stats.sightChecks);
            peak.activeGroups=std::max(peak.activeGroups,stats.activeGroups);
            peak.crossingWaits=std::max(peak.crossingWaits,stats.crossingWaits);
            for(size_t i=0;i<peak.activityCounts.size();++i)peak.activityCounts[i]=std::max(peak.activityCounts[i],stats.activityCounts[i]);
        }
        std::sort(samples.begin(),samples.end());
        const auto quantile=[&](double fraction){return samples[size_t(fraction*double(samples.size()-1))];};
        std::cout<<std::fixed<<std::setprecision(3)<<"scenario="<<scenario<<"; people="<<game.pedestrians.size()<<"; vehicles="<<game.vehicles.size()
                 <<"; frames="<<samples.size()<<"; first_us="<<firstUs<<"; median_us="<<quantile(.5)<<"; p95_us="<<quantile(.95)
                 <<"; p99_us="<<quantile(.99)<<"; max_us="<<samples.back()<<"; decisions="<<peak.decisions<<"; routes="<<peak.routeSearches
                 <<"; expansions="<<peak.routeExpansions<<"; sight="<<peak.sightChecks<<"; groups="<<peak.activeGroups<<"; waits="<<peak.crossingWaits
                 <<"; fleeing="<<peak.activityCounts[size_t(mc::PedestrianActivity::Flee)]<<'\n';
    }
}
