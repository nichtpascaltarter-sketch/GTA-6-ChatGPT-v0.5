#include "../../src/game.h"
#include <iomanip>
#include <iostream>

namespace {
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
    game.completedMissions = 5;
    game.player = {-3200, 4, -1190};
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
    if (game.activeMission != 5 || game.occupied < 0 || game.vehicles.empty() || game.pedestrians.empty())
        return 1;

    const size_t craftIndex = size_t(game.occupied);
    int previousStage = -1, leg = 0, frame = 0;
    for (; frame < 21500 && game.activeMission == 5; ++frame) {
        // From acceptance onward, change state only through the public Input API.
        const mc::Vehicle& plane = game.vehicles[craftIndex];
        mc::Vec3 target = game.missionTarget();
        if (game.missionStage == 3) {
            const mc::Vec3 circuit[] = {{-4000, 110, -1800}, {-3200, 65, -2300},
                                       {-3200, 35, -1600}, {-3200, 0, -600}};
            target = circuit[leg];
            mc::Vec3 delta = target - plane.position;
            delta.y = 0;
            if (mc::length(delta) < 150 && leg < 3) target = circuit[++leg];
            if (leg == 3) target.y = mc::clamp((-1300 - plane.position.z) * .06f, 0, 35);
            target.y += game.world.height(target.x, target.z);
        }
        const float altitude = plane.position.y - game.world.height(plane.position.x, plane.position.z);
        const float desiredAltitude = target.y - game.world.height(target.x, target.z);
        const float headingError = wrap(std::atan2(target.x - plane.position.x,
                                                   target.z - plane.position.z) - plane.yaw);
        const float desiredSpeed = game.missionStage == 3 && leg >= 2 ? 38.0f : 45.0f;
        const float throttle = game.missionStage == 3 && leg == 3 && altitude < 5 ? 0.0f :
            (game.missionStage < 3 && altitude < 5 ? 1.0f :
             mc::clamp((plane.speed * (.018f + .0017f * plane.speed) +
                 9.81f * std::sin(plane.pitch) + (desiredSpeed - plane.speed) * .8f) / 11.5f, 0, 1));
        float desiredClimb = mc::clamp((desiredAltitude - altitude) * .25f, -7, 7);
        if (game.missionStage == 3 && leg == 3 && altitude < 10) desiredClimb = -2.0f;
        mc::Input input;
        input.moveX = mc::clamp(headingError * 3, -1, 1);
        input.moveY = plane.throttle < throttle - .01f ? 1.0f :
            plane.throttle > throttle + .01f ? -1.0f : 0.0f;
        input.sprint = plane.velocity.y < desiredClimb - .5f;
        input.brake = plane.velocity.y > desiredClimb + .5f;
        if (game.missionStage == 3 && leg == 3 && altitude < .12f) {
            input.brake = true;
            input.sprint = false;
            input.moveY = -1;
        }
        game.update(input, dt);
        if (frame % 600 == 0 || game.missionStage != previousStage ||
            (game.missionStage == 3 && leg == 3 && frame % 120 == 0)) {
            previousStage = game.missionStage;
            std::cout << "input_seconds=" << float(frame + 1) * dt
                      << " stage=" << game.missionStage << " circuit_leg=" << leg
                      << " position=" << plane.position.x << ',' << plane.position.y << ',' << plane.position.z
                      << " speed=" << plane.speed << " pre_step_agl=" << altitude
                      << " craft_health=" << plane.health << " pilot_health=" << game.health << '\n';
        }
    }
    const bool passed = game.completedMissions == 6 && game.activeMission == -1 &&
        game.money == initialMoney + 2600 && game.vehicles[craftIndex].health == 100 && game.health == 100;
    std::cout << "result=" << (passed ? "PASS" : "FAIL")
              << " input_seconds=" << float(frame) * dt
              << " completed=" << game.completedMissions << " reward=" << game.money - initialMoney
              << " message=" << game.message << '\n';
    return passed ? 0 : 1;
}
