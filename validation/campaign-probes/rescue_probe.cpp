#include "../../src/game.h"
#include <iomanip>
#include <iostream>

namespace {
float planarDistance(mc::Vec3 a, mc::Vec3 b) {
    a.y = b.y = 0;
    return mc::length(a - b);
}
float wrap(float angle) {
    while (angle > mc::Pi) angle -= 2 * mc::Pi;
    while (angle < -mc::Pi) angle += 2 * mc::Pi;
    return angle;
}
}

int main() {
    constexpr float dt = 1.0f / 60;
    mc::Game game;
    game.initialize();
    // Retain the initialized ambient fleet and pedestrians throughout the run.
    game.completedMissions = 4;
    game.player = {2674, .4f, 768};
    const int initialMoney = game.money;
    mc::Input accept;
    accept.mission = true;
    game.update(accept, dt);
    mc::Input enter;
    enter.interact = true;
    game.update(enter, dt);
    std::cout << std::fixed << std::setprecision(3)
              << "population=enabled; vehicle=acceptance-serviced loan\n"
              << "vehicles=" << game.vehicles.size() << " pedestrians=" << game.pedestrians.size() << '\n'
              << "accepted=" << game.activeMission << " occupied=" << game.occupied << '\n';
    if (game.activeMission != 4 || game.occupied < 0 || game.vehicles.empty() || game.pedestrians.empty())
        return 1;

    const size_t craftIndex = size_t(game.occupied);
    int previousStage = -1;
    int frame = 0;
    for (; frame < 14400 && game.activeMission == 4; ++frame) {
        // From acceptance onward, change state only through the public Input API.
        const mc::Vehicle& boat = game.vehicles[craftIndex];
        const mc::Vec3 target = game.missionStage < 2
            ? mc::Vec3{3080, mc::World::WaterLevel, 1080}
            : mc::Vec3{2682, mc::World::WaterLevel, 768};
        const float distance = planarDistance(boat.position, target);
        const float headingError = wrap(std::atan2(target.x - boat.position.x,
                                                   target.z - boat.position.z) - boat.yaw);
        float desiredSpeed = std::min(14.0f, std::max(0.0f, (distance - 2) * .22f));
        if (std::abs(headingError) > .65f) desiredSpeed = std::min(desiredSpeed, 3.0f);
        mc::Input input;
        input.moveX = mc::clamp(headingError * 2, -1, 1);
        input.moveY = mc::clamp(((desiredSpeed - boat.speed) * .8f + boat.speed *
            (.075f + .0065f * std::abs(boat.speed))) / 5.4f, -1, 1);
        input.brake = boat.speed > desiredSpeed + 1 || desiredSpeed < .2f;
        game.update(input, dt);
        if (frame % 600 == 0 || game.missionStage != previousStage) {
            previousStage = game.missionStage;
            std::cout << "input_seconds=" << float(frame + 1) * dt
                      << " stage=" << game.missionStage << " remaining_distance=" << distance
                      << " speed=" << boat.speed << " craft_health=" << boat.health
                      << " pilot_health=" << game.health << '\n';
        }
    }
    const bool passed = game.completedMissions == 5 && game.activeMission == -1 &&
        game.money == initialMoney + 1800 && game.vehicles[craftIndex].health == 100 && game.health == 100;
    std::cout << "result=" << (passed ? "PASS" : "FAIL")
              << " input_seconds=" << float(frame) * dt
              << " completed=" << game.completedMissions << " reward=" << game.money - initialMoney
              << " message=" << game.message << '\n';
    return passed ? 0 : 1;
}
