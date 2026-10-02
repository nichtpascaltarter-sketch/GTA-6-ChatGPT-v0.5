#pragma once
#include "world.h"
#include <array>
#include <limits>

namespace mc {
enum class PedestrianActivity : uint8_t {Walk,Wait,Sit,Work,Carry,Talk,Startle,Flee};
struct PedestrianStats {
    std::array<uint32_t,8> activityCounts{};
    uint32_t persistentResidents=0,activeGroups=0,crossingWaits=0,committedCrossings=0;
    uint32_t decisions=0,routeSearches=0,routeExpansions=0,neighborChecks=0,sightChecks=0;
};
struct PedestrianBrain {
    uint32_t identity=0,home=0,work=0,destination=0,reserved=0,group=0,node=0,lastStimulus=0;
    std::vector<uint32_t> route;
    uint32_t routeOffset=0,crossingFrom=0,crossingTo=0;
    int schedule=-1;
    float decisionDelay=0,dwell=0,blockedTime=0,crossingWait=0,reactionTime=0;
    Vec3 threat,escape;
    bool persistent=false,hasThreat=false,crossingCommitted=false;
};
struct PedestrianStimulus {Vec3 position;float age=0,radius=0;uint32_t serial=0;};
struct PedestrianCrossing {uint32_t id=0;Vec3 from,to;};
struct PedestrianSimulation {
    PedestrianNetwork network;
    std::vector<PedestrianBrain> brains;
    std::vector<PedestrianStimulus> stimuli;
    std::vector<PedestrianCrossing> crossings;
    PedestrianStats stats;
    uint64_t revision=std::numeric_limits<uint64_t>::max();
    uint32_t nextIdentity=1,stimulusSerial=0,cursor=4;
};
}
