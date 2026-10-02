#pragma once
#include "mc_math.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mc {
class World;
struct LawObstacle {Vec3 min,max;};

// Implementations must treat unavailable collision as unknown, never as clear.
// These queries contain geometry only; there is deliberately no suspect lookup.
class LawSpace {
public:
    virtual ~LawSpace()=default;
    // Dynamic adapters may exclude the observer's own body, never nearby actors.
    // This carries identity only and does not expose a suspect position.
    virtual void setObserver(uint32_t) const {}
    virtual bool ready(Vec3 position) const=0;
    virtual bool project(Vec3 desired,float bodyRadius,Vec3& ground) const=0;
    virtual bool walkClear(Vec3 from,Vec3 to,float bodyRadius) const=0;
    virtual bool lineClear(Vec3 from,Vec3 to) const=0;
    virtual size_t obstacles(Vec3 center,float radius,std::span<LawObstacle> output) const=0;
};

class WorldLawSpace final:public LawSpace {
public:
    explicit WorldLawSpace(const World& world):world_(world){}
    bool ready(Vec3) const override;
    bool project(Vec3,float,Vec3&) const override;
    bool walkClear(Vec3,Vec3,float) const override;
    bool lineClear(Vec3,Vec3) const override;
    size_t obstacles(Vec3,float,std::span<LawObstacle>) const override;
private:
    const World& world_;
};

struct LawPath {
    static constexpr size_t Capacity=32;
    std::array<Vec3,Capacity> points{};
    uint32_t count=0;
};
enum class LawPathStatus:uint8_t {Idle,Building,Ready,Failed};
struct LawNavigationStats {uint32_t checks=0,expansions=0,nodes=0;};

// One incremental visibility-graph job. All storage and per-step query work is bounded.
class LawNavigation {
public:
    static constexpr size_t MaxObstacles=24,MaxNodes=98;
    bool begin(const LawSpace&,Vec3 from,Vec3 destination,float bodyRadius=.35f);
    void step(const LawSpace&,uint32_t queryBudget=12);
    void clear();
    LawPathStatus status() const{return status_;}
    const LawPath& path() const{return path_;}
    LawNavigationStats stats() const{return stats_;}
    Vec3 destination() const{return destination_;}
private:
    void connect();
    void finish(int node);
    LawPathStatus status_=LawPathStatus::Idle;
    LawPath path_{};
    LawNavigationStats stats_{};
    std::array<LawObstacle,MaxObstacles> obstacles_{};
    std::array<Vec3,MaxNodes> nodes_{};
    std::array<float,MaxNodes> costs_{};
    std::array<int,MaxNodes> previous_{};
    std::array<bool,MaxNodes> settled_{};
    // 0 no edge, 1 unchecked candidate, 2 clear, 3 blocked.
    std::array<uint8_t,MaxNodes*MaxNodes> edges_{};
    uint32_t obstacleCount_=0,corner_=0,nodeCount_=0,neighbor_=0;
    int current_=-1;
    float radius_=.35f;
    Vec3 destination_{};
    bool connected_=false;
};
}
