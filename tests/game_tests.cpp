#include "../src/game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
static_assert(sizeof(mc::Light) == 48, "light data must match its GPU buffer stride");

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(float a, float b, float tolerance = 0.001f) {
    return std::fabs(a - b) <= tolerance;
}

bool finite(mc::Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void tick(mc::Game& game, const mc::Input& input = {}, int frames = 1) {
    for (int i = 0; i < frames; ++i) game.update(input, 1.0f / 60.0f);
}

void verifyMesh(const mc::Mesh& mesh) {
    require(!mesh.vertices.empty(), "generated mesh has no vertices");
    require(!mesh.indices.empty(), "generated mesh has no indices");
    require(mesh.indices.size() % 3 == 0, "mesh has an incomplete triangle");
    for (const mc::Vertex& vertex : mesh.vertices) {
        require(finite(vertex.position), "mesh position is not finite");
        require(finite(vertex.normal), "mesh normal is not finite");
        require(finite(vertex.color), "mesh color is not finite");
        require(std::isfinite(vertex.material), "mesh material is not finite");
    }
    for (uint32_t index : mesh.indices)
        require(index < mesh.vertices.size(), "mesh index exceeds its vertex buffer");
}

size_t verifyDynamicMesh(const mc::Mesh& mesh, const char* pose) {
    verifyMesh(mesh);
    const size_t triangles = mesh.indices.size() / 3;
    if (triangles >= 90000)
        throw std::runtime_error(std::string(pose) + " dynamic mesh exceeds triangle budget: " +
                                 std::to_string(triangles));
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const mc::Vertex& a = mesh.vertices[mesh.indices[i]];
        const mc::Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const mc::Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const mc::Vec3 area = mc::cross(b.position - a.position, c.position - a.position);
        // Collapsed pole and fan triangles do not have a meaningful surface normal.
        if (mc::dot(area, area) <= 1e-14f) continue;
        for (const mc::Vertex* vertex : {&a, &b, &c}) {
            if (!close(mc::length(vertex->normal), 1, 0.02f))
                throw std::runtime_error(std::string(pose) +
                    " dynamic mesh has a non-unit normal on triangle " + std::to_string(i / 3));
        }
    }
    return triangles;
}

void initializationAndGeometry() {
    mc::Game game;
    game.initialize();
    require(!game.world.chunks.empty(), "initialization did not stream the world");
    require(!game.vehicles.empty(), "initialization did not create vehicles");
    require(!game.pedestrians.empty(), "initialization did not create pedestrians");
    require(!mc::Game::missions().empty(), "campaign has no missions");
    require(finite(game.player), "player spawn is not finite");
    require(finite(game.cameraEye()) && finite(game.cameraTarget()), "camera is not finite");
    require(mc::length(game.cameraEye() - game.cameraTarget()) > 0.1f,
            "camera eye and target are coincident");
    const size_t spawnTriangles = verifyDynamicMesh(game.dynamicMesh(), "spawn");
    verifyMesh(game.world.combinedMesh());
    game.player = {128, game.world.height(128, 128), 128};
    require(game.world.biome(game.player.x, game.player.z) == mc::Biome::Downtown,
            "urban mesh fixture is outside the downtown biome");
    tick(game, {}, 70);
    const size_t urbanTriangles = verifyDynamicMesh(game.dynamicMesh(), "urban teleport");
    std::cout << "Dynamic mesh triangles: spawn " << spawnTriangles
              << ", urban teleport " << urbanTriangles << '\n';
}

void verifyLights(const std::vector<mc::Light>& lights) {
    require(lights.size() <= 64, "light list exceeds the GPU light capacity");
    for (const mc::Light& light : lights) {
        require(finite(light.position) && finite(light.direction) && finite(light.color),
                "light contains a nonfinite vector");
        require(std::isfinite(light.radius) && light.radius > 0,
                "light has an invalid radius");
        require(std::isfinite(light.intensity) && light.intensity > 0,
                "light has an invalid intensity");
        require(light.color.x >= 0 && light.color.y >= 0 && light.color.z >= 0 &&
                mc::length(light.color) > 0, "light has an invalid emission color");
        require(close(mc::length(light.direction), 1, 0.002f),
                "light direction is not normalized");
        require(std::isfinite(light.cone) && light.cone >= -1 && light.cone <= 1,
                "light cone is invalid");
    }
}

const mc::Light* lightAt(const std::vector<mc::Light>& lights, mc::Vec3 position) {
    for (const mc::Light& light : lights)
        if (mc::length(light.position - position) < 0.002f) return &light;
    return nullptr;
}

void lightingActivationAndTransforms() {
    mc::Game game;
    game.player = {};
    game.world.chunks.emplace_back();
    mc::Light lamp;
    lamp.position = {4, 7, 2};
    lamp.direction = {0, -2, 0};
    lamp.cone = -0.15f;
    game.world.chunks[0].lights.push_back(lamp);
    mc::Vehicle car;
    car.position = {12, 0, 4};
    car.parked = true;
    game.vehicles.push_back(car);
    mc::Vehicle bike;
    bike.position = {-12, 0, 4};
    bike.kind = mc::VehicleKind::Motorcycle;
    bike.parked = true;
    game.vehicles.push_back(bike);

    game.dayTime = 12;
    require(game.lightSources().empty(), "ordinary lighting remains on at noon");
    game.dayTime = 0;
    const auto night = game.lightSources();
    verifyLights(night);
    require(night.size() == 4, "midnight did not enable one lamp and three headlights");
    const mc::Light* worldLamp = lightAt(night, lamp.position);
    require(worldLamp != nullptr, "world lamp is absent at midnight");
    require(close(worldLamp->cone, -0.15f), "broad world spotlight cone was changed");
    for (float side : {-1.0f, 1.0f}) {
        const mc::Vec3 offset{side * 0.535f, 0.635f, 2.10f};
        const mc::Light* headlight = lightAt(night, car.position + offset);
        require(headlight != nullptr, "car headlight does not match its visible emitter");
        require(close(headlight->radius, 45) && close(headlight->cone, 0.85f),
                "car headlight coverage is incorrect");
        mc::Vec3 horizontal = headlight->direction;
        horizontal.y = 0;
        require(mc::dot(mc::normalized(horizontal), mc::forward(car.yaw)) > 0.999f,
                "car headlight does not point forward");
    }
    require(lightAt(night, bike.position + mc::Vec3{0, 1, 0.755f}) != nullptr,
            "motorcycle headlight does not match its visible emitter");

    game.vehicles[0].yaw = mc::Pi * 0.5f;
    const auto rotated = game.lightSources();
    verifyLights(rotated);
    for (float side : {-1.0f, 1.0f}) {
        const float yaw = game.vehicles[0].yaw;
        const mc::Vec3 offset = mc::right(yaw) * (side * 0.535f) +
            mc::Vec3{0, 0.635f, 0} + mc::forward(yaw) * 2.10f;
        const mc::Light* headlight = lightAt(rotated, car.position + offset);
        require(headlight != nullptr, "turning a car did not rotate its headlight positions");
        mc::Vec3 horizontal = headlight->direction;
        horizontal.y = 0;
        require(mc::dot(mc::normalized(horizontal), mc::forward(yaw)) > 0.999f,
                "turning a car did not rotate its headlight direction");
    }

    game.vehicles[0].health = game.vehicles[1].health = 0;
    require(game.lightSources().size() == 1, "destroyed vehicles still emit headlights");
    game.world.chunks[0].lights.clear();
    game.vehicles.resize(1);
    game.vehicles[0].health = 100;
    game.vehicles[0].police = true;
    game.dayTime = 12;
    game.wanted = 0;
    require(game.lightSources().empty(), "idle daytime patrol has active emergency lights");
    game.wanted = 1;
    const auto pursuit = game.lightSources();
    verifyLights(pursuit);
    require(!pursuit.empty(), "daytime pursuit did not activate police emergency lights");
    for (float side : {-1.0f, 1.0f}) {
        const float yaw = game.vehicles[0].yaw;
        const mc::Vec3 headlight = car.position + mc::right(yaw) * (side * 0.535f) +
            mc::Vec3{0, 0.635f, 0} + mc::forward(yaw) * 2.10f;
        require(lightAt(pursuit, headlight) == nullptr,
                "daytime emergency lights also enabled ordinary headlights");
    }
}

void lightingCapacityRangeAndOrder() {
    mc::Game game;
    game.player = {};
    game.dayTime = 0;
    game.world.chunks.emplace_back();
    mc::Light lamp;
    lamp.position = {lamp.radius + 71, 0, 0};
    game.world.chunks[0].lights.push_back(lamp);
    require(game.lightSources().empty(), "world light beyond radius plus 70 was retained");
    game.world.chunks[0].lights[0].position.x = lamp.radius + 69;
    require(game.lightSources().size() == 1, "nearby world light was culled too early");
    game.world.chunks[0].lights.clear();
    mc::Vehicle distant;
    distant.position = {1000, 0, 0};
    game.vehicles.push_back(distant);
    require(game.lightSources().empty(), "distant vehicle headlights were retained");
    game.vehicles.clear();

    mc::Light invalid;
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    game.world.chunks[0].lights.push_back(invalid);
    invalid = mc::Light{};
    invalid.intensity = std::numeric_limits<float>::infinity();
    game.world.chunks[0].lights.push_back(invalid);
    invalid = mc::Light{};
    invalid.radius = 0;
    game.world.chunks[0].lights.push_back(invalid);
    invalid = mc::Light{};
    invalid.color.y = -1;
    game.world.chunks[0].lights.push_back(invalid);
    require(game.lightSources().empty(), "invalid light data reached the renderer");
    game.world.chunks[0].lights.clear();

    for (int i = 0; i < 100; ++i) {
        lamp.position = {float(i % 10) * 3 - 15, 5, float(i / 10) * 3 - 15};
        game.world.chunks[0].lights.push_back(lamp);
    }
    const auto first = game.lightSources();
    const auto second = game.lightSources();
    verifyLights(first);
    verifyLights(second);
    require(first.size() == 64 && second.size() == 64,
            "dense light selection did not respect its 64-light capacity");
    for (size_t i = 0; i < first.size(); ++i) {
        require(mc::length(first[i].position - second[i].position) < 0.0001f &&
                mc::length(first[i].direction - second[i].direction) < 0.0001f &&
                mc::length(first[i].color - second[i].color) < 0.0001f &&
                close(first[i].radius, second[i].radius) &&
                close(first[i].intensity, second[i].intensity) &&
                close(first[i].cone, second[i].cone),
                "identical state produced a different light selection or order");
    }
}

void movementAndPause() {
    mc::Game game;
    game.initialize();
    game.player = {0, game.world.height(0, 0), 0};
    game.yaw = 0;
    game.vehicles.clear();
    game.pedestrians.clear();
    const mc::Vec3 start = game.player;
    mc::Input walk;
    walk.moveY = 1;
    tick(game, walk, 30);
    require(finite(game.player), "walking produced a nonfinite position");
    require(mc::length(game.player - start) > 0.1f, "walking did not move the player");
    require(mc::length(game.player - start) < 10, "walking speed is unbounded");

    game.paused = true;
    const mc::Vec3 pausedPosition = game.player;
    const float pausedTime = game.time;
    const float pausedDay = game.dayTime;
    tick(game, walk, 120);
    require(mc::length(game.player - pausedPosition) < 0.0001f, "paused movement advanced");
    require(close(game.time, pausedTime), "paused simulation clock advanced");
    require(close(game.dayTime, pausedDay), "paused day/night clock advanced");

    game.paused = false;
    const mc::Vec3 beforeLongFrame = game.player;
    const float beforeLongTime = game.time;
    game.update(walk, 1000);
    require(finite(game.player), "long frame produced a nonfinite position");
    require(game.time - beforeLongTime <= 0.101f, "long frame was not clamped");
    require(mc::length(game.player - beforeLongFrame) < 2, "long frame teleported the player");
    const float beforeNegativeTime = game.time;
    const mc::Vec3 beforeNegativePosition = game.player;
    game.update(walk, -1);
    require(close(game.time, beforeNegativeTime), "negative delta changed simulation time");
    require(mc::length(game.player - beforeNegativePosition) < 0.0001f,
            "negative delta moved the player");
}

void vehicleInteraction() {
    mc::Game game;
    game.initialize();
    game.pedestrians.clear();
    game.vehicles.clear();
    mc::Vehicle car;
    car.position = {0, game.world.height(0, 0), 0};
    car.parked = true;
    game.vehicles.push_back(car);
    game.player = car.position + mc::Vec3{1, 0, 0};
    mc::Input interact;
    interact.interact = true;
    tick(game, interact);
    require(game.occupied == 0, "interact did not enter the nearby car");
    tick(game, interact, 2);
    require(game.occupied == 0, "holding interact repeatedly toggled vehicle occupancy");
    const mc::Vec3 before = game.vehicles[0].position;
    mc::Input accelerate;
    accelerate.moveY = 1;
    tick(game, accelerate, 60);
    require(finite(game.vehicles[0].position), "driving produced a nonfinite position");
    require(mc::length(game.vehicles[0].position - before) > 0.1f, "car did not accelerate");
    require(mc::length(game.player - game.vehicles[0].position) < 3,
            "occupied player did not follow the vehicle");
    game.vehicles[0].speed = 0;
    game.vehicles[0].velocity = {};
    tick(game);
    tick(game, interact);
    require(game.occupied == -1, "interact did not exit the car");
    tick(game, interact, 2);
    require(game.occupied == -1, "holding interact re-entered a car after exiting");
    require(finite(game.player), "exiting produced a nonfinite player position");
    require(mc::length(game.player - game.vehicles[0].position) < 8,
            "exiting placed the player too far from the vehicle");
}

void boatHandlingAndSwimming() {
    mc::Game game;
    mc::Vehicle boat;
    boat.kind = mc::VehicleKind::Boat;
    boat.position = {2678, mc::World::WaterLevel, 768};
    boat.parked = true;
    game.vehicles.push_back(boat);
    game.player = boat.position + mc::Vec3{2, 0, 0};
    require(game.world.waterDepth(boat.position.x, boat.position.z) > 0.9f,
            "starter boat is not in navigable water");
    mc::Input interact;
    interact.interact = true;
    tick(game, interact);
    require(game.occupied == 0, "nearby boat could not be boarded");
    mc::Input thrust;
    thrust.moveY = 1;
    tick(game, thrust, 240);
    require(game.vehicles[0].speed > 5, "boat thrust did not build speed");
    require(mc::length(game.vehicles[0].position - boat.position) > 10,
            "boat did not travel across the water");
    require(std::fabs(game.vehicles[0].position.y - mc::World::WaterLevel) < 0.4f,
            "boat did not float near the water surface");
    const float straightYaw = game.vehicles[0].yaw;
    thrust.moveX = 0.7f;
    tick(game, thrust, 120);
    require(std::fabs(mc::wrapAngle(game.vehicles[0].yaw - straightYaw)) > 0.2f,
            "boat rudder did not turn the moving hull");
    require(finite(game.vehicles[0].position) && finite(game.vehicles[0].velocity) &&
            std::isfinite(game.vehicles[0].pitch) && std::isfinite(game.vehicles[0].roll),
            "boat handling produced invalid state");
    verifyDynamicMesh(game.dynamicMesh(), "occupied turning boat");
    mc::Input brake;
    brake.brake = true;
    tick(game, brake, 300);
    require(std::fabs(game.vehicles[0].speed) < 0.5f, "boat braking did not stop the hull");
    tick(game, interact);
    require(game.occupied == -1, "stopped boat could not be exited into the water");
    const mc::Vec3 swimStart = game.player;
    mc::Input swim;
    swim.moveY = 1;
    tick(game, swim, 30);
    require(mc::length(game.player - swimStart) > 0.5f, "swimming did not move the player");
    require(game.player.y > mc::World::WaterLevel - 1.5f &&
            game.player.y < mc::World::WaterLevel, "swimming player sank below the surface");
    tick(game, interact);
    require(game.occupied == 0, "swimming player could not reboard the nearby boat");
}

void aircraftFlightAndLanding() {
    mc::Game game;
    mc::Vehicle aircraft;
    aircraft.kind = mc::VehicleKind::Aircraft;
    aircraft.position = {-3200, game.world.height(-3200, -1190), -1190};
    game.vehicles.push_back(aircraft);
    game.player = aircraft.position;
    game.occupied = 0;
    mc::Input takeoff;
    takeoff.moveY = 1;
    takeoff.sprint = true;
    tick(game, takeoff, 900);
    const mc::Vehicle& flying = game.vehicles[0];
    require(game.occupied == 0 && flying.health > 0, "aircraft failed its runway takeoff");
    require(flying.position.y - game.world.height(flying.position.x, flying.position.z) > 15,
            "aircraft did not climb clear of the runway");
    require(flying.speed > 25 && flying.throttle > 0.99f,
            "aircraft throttle did not produce flight speed");
    const float throttle = flying.throttle;
    tick(game, {}, 60);
    require(close(game.vehicles[0].throttle, throttle), "aircraft throttle was not persistent");
    const float beforeTurn = game.vehicles[0].yaw;
    mc::Input bank;
    bank.moveX = 1;
    tick(game, bank, 120);
    require(game.vehicles[0].roll > 0.2f &&
            std::fabs(mc::wrapAngle(game.vehicles[0].yaw - beforeTurn)) > 0.05f,
            "banking did not roll and turn the aircraft");
    verifyDynamicMesh(game.dynamicMesh(), "occupied banked aircraft");
    mc::Input interact;
    interact.interact = true;
    tick(game, interact);
    require(game.occupied == 0, "aircraft permitted an exit while airborne");

    mc::Vehicle& plane = game.vehicles[0];
    plane.position = {-3200, game.world.height(-3200, -1000) + 140, -1000};
    plane.yaw = plane.pitch = plane.roll = 0;
    plane.speed = 12;
    plane.velocity = {0, 0, 12};
    plane.throttle = 0.2f;
    game.player = plane.position;
    const float stallAltitude = plane.position.y;
    mc::Input cutThrottle;
    cutThrottle.moveY = -1;
    tick(game, cutThrottle, 120);
    require(close(plane.throttle, 0), "throttle-down input did not cut aircraft power");
    require(plane.position.y < stallAltitude - 2 && plane.velocity.y < -1,
            "aircraft retained altitude without sufficient power and airspeed");
    require(finite(plane.position) && finite(plane.velocity), "stall produced invalid motion");

    plane.position = {-3200, game.world.height(-3200, -1190) + 0.2f, -1190};
    plane.yaw = plane.pitch = plane.roll = plane.throttle = 0;
    plane.speed = 5;
    plane.velocity = {0, -1, 5};
    game.player = plane.position;
    const float healthBeforeLanding = plane.health;
    tick(game, {}, 60);
    require(std::fabs(plane.position.y - game.world.height(plane.position.x, plane.position.z)) < 0.11f,
            "gentle landing did not settle onto the runway");
    require(close(plane.health, healthBeforeLanding), "gentle landing damaged the aircraft");
    mc::Input wheelBrake;
    wheelBrake.brake = true;
    tick(game, wheelBrake, 120);
    require(plane.speed < 0.1f, "wheel braking did not stop the landed aircraft");
    tick(game, interact);
    require(game.occupied == -1, "landed and stopped aircraft could not be exited");
}

void hardAircraftLandingsAcrossFrameOffsets() {
    mc::Game game;
    const float ground = game.world.height(-3200, -1000);
    std::string missedImpacts;
    for (float drop : {18.0f, 18.1f, 18.2f, 20.0f}) {
        mc::Vehicle aircraft;
        aircraft.kind = mc::VehicleKind::Aircraft;
        aircraft.position = {-3200, ground + drop, -1000};
        game.vehicles.assign(1, aircraft);
        game.player = {-3200, ground, -1000};
        game.occupied = -1;
        tick(game, {}, 240);
        const mc::Vehicle& landed = game.vehicles[0];
        require(finite(landed.position) && finite(landed.velocity),
                "hard aircraft landing produced nonfinite state");
        require(close(landed.position.y, ground, 0.02f) && close(landed.velocity.y, 0),
                "hard aircraft landing did not settle on the runway");
        if (!close(landed.health, 0)) missedImpacts += std::to_string(drop) + "m ";
    }

    mc::Vehicle occupiedAircraft;
    occupiedAircraft.kind = mc::VehicleKind::Aircraft;
    occupiedAircraft.position = {-3200, ground + 18, -1000};
    game.vehicles.assign(1, occupiedAircraft);
    game.player = occupiedAircraft.position;
    game.occupied = 0;
    tick(game, {}, 240);
    if (!close(game.vehicles[0].position.y, ground, 0.02f))
        missedImpacts += "occupied 18m stopped above runway; ";
    if (!close(game.vehicles[0].health, 0)) missedImpacts += "occupied 18m";
    if (!missedImpacts.empty())
        throw std::runtime_error("hard landing skipped aircraft impact damage at: " + missedImpacts);
}

void aircraftAltitudeSeparatesContacts() {
    mc::Game game;
    const float ground = game.world.height(-3200, -1000);
    mc::Vehicle aircraft;
    aircraft.kind = mc::VehicleKind::Aircraft;
    aircraft.position = {-3200, ground + 50, -1000};
    aircraft.speed = 40;
    aircraft.velocity = {0, 0, 40};
    aircraft.throttle = 0.6f;
    game.vehicles.push_back(aircraft);
    mc::Vehicle car;
    car.position = {-3199, ground, -1000};
    car.parked = true;
    game.vehicles.push_back(car);
    mc::Pedestrian pedestrian;
    pedestrian.position = {-3200, ground, -999.3f};
    game.pedestrians.push_back(pedestrian);
    game.player = aircraft.position;
    game.occupied = 0;
    tick(game);
    require(close(game.vehicles[0].health, 100) && close(game.vehicles[1].health, 100),
            "aircraft collided with a road vehicle 50 metres below it");
    require(close(game.vehicles[0].position.x, aircraft.position.x) && game.vehicles[0].speed > 39,
            "ground traffic deflected or slowed an aircraft overhead");
    require(close(game.pedestrians[0].health, 100) && game.wanted == 0,
            "aircraft overhead struck a pedestrian on the ground");
}

void ditchedAircraftExitAndSwimming() {
    mc::Game game;
    mc::Vehicle aircraft;
    aircraft.kind = mc::VehicleKind::Aircraft;
    aircraft.position = {3200, mc::World::WaterLevel, 1000};
    aircraft.health = 0;
    game.vehicles.push_back(aircraft);
    game.player = aircraft.position;
    game.occupied = 0;
    game.health = 40;
    game.money = 1234;
    require(game.world.waterDepth(aircraft.position.x, aircraft.position.z) > 25,
            "ditched aircraft fixture is not over deep ocean");
    mc::Input interact;
    interact.interact = true;
    tick(game, interact);
    require(game.occupied == -1, "destroyed ditched aircraft could not be exited");
    require(close(game.health, 40) && game.money == 1234,
            "deep-water exit injured or respawned the player");
    require(mc::length(game.player - aircraft.position) < 10 &&
            close(game.player.y, mc::World::WaterLevel - 1.1f, 0.05f),
            "deep-water exit placed the player away from the water surface");
    const mc::Vec3 swimStart = game.player;
    mc::Input swim;
    swim.moveY = 1;
    tick(game, swim, 30);
    require(mc::length(game.player - swimStart) > 0.5f &&
            close(game.player.y, mc::World::WaterLevel - 1.1f, 0.05f),
            "player could not swim at the surface after leaving a ditched aircraft");
    require(close(game.health, 40) && game.money == 1234,
            "swimming after a deep-water exit caused injury or respawn");

    game = mc::Game{};
    aircraft.position.y = mc::World::WaterLevel + 10;
    aircraft.health = 100;
    game.vehicles.push_back(aircraft);
    game.player = aircraft.position;
    game.occupied = 0;
    game.health = 40;
    game.money = 1234;
    tick(game, interact);
    require(game.occupied == 0, "airborne aircraft allowed an exit above deep water");
    require(game.player.y > mc::World::WaterLevel + 9 &&
            close(game.health, 40) && game.money == 1234,
            "refused airborne exit changed the player's safety or progress");
}

void aircraftExitPrefersDock() {
    mc::Game game;
    mc::Vehicle aircraft;
    aircraft.kind = mc::VehicleKind::Aircraft;
    aircraft.position = {2678, mc::World::WaterLevel, 768};
    aircraft.yaw = mc::Pi;
    aircraft.health = 0;
    game.vehicles.push_back(aircraft);
    game.player = aircraft.position;
    game.occupied = 0;
    game.health = 40;
    game.money = 1234;
    game.world.stream(game.player);
    const mc::Vec3 waterSide = aircraft.position - mc::right(aircraft.yaw) * 3.8f;
    mc::Vec3 dockSide = aircraft.position + mc::right(aircraft.yaw) * 3.8f;
    dockSide.y = game.world.height(dockSide.x, dockSide.z);
    require(game.world.waterDepth(waterSide.x, waterSide.z) > 0.9f &&
            game.world.height(waterSide.x, waterSide.z) < mc::World::WaterLevel,
            "first aircraft exit fixture is not in the water");
    require(dockSide.y > mc::World::WaterLevel && !game.world.blocked(dockSide, 0.35f),
            "opposite aircraft exit fixture is not a clear dock");
    mc::Input interact;
    interact.interact = true;
    tick(game, interact);
    require(game.occupied == -1, "aircraft beside an open dock could not be exited");
    require(game.player.y > mc::World::WaterLevel &&
            close(game.player.y, game.world.height(game.player.x, game.player.z)),
            "aircraft exit chose water despite a clear dock on the opposite side");
    require(close(game.health, 40) && game.money == 1234,
            "dock exit injured or respawned the player");
}

void weaponsAndRadio() {
    mc::Game game;
    game.initialize();
    game.vehicles.clear();
    game.pedestrians.clear();
    mc::Input fire;
    fire.fire = true;
    const int initialAmmo = game.ammo;
    tick(game, fire);
    require(game.ammo == initialAmmo - 1, "firing did not consume exactly one round");
    tick(game, fire);
    require(game.ammo == initialAmmo - 1, "weapon ignored its firing cooldown");
    tick(game, fire, 120);
    require(game.ammo >= 0 && game.ammo < initialAmmo - 1,
            "holding fire did not respect ammunition bounds");
    require(finite(game.shotEnd), "weapon trace endpoint is not finite");

    const int totalAmmo = game.ammo + game.reserveAmmo;
    mc::Input reload;
    reload.reload = true;
    tick(game, reload);
    tick(game, {}, 180);
    require(game.ammo > 0, "reload did not restore ammunition");
    require(game.ammo + game.reserveAmmo == totalAmmo, "reload created or destroyed rounds");
    game.ammo = game.reserveAmmo = 0;
    tick(game, fire, 30);
    tick(game, reload);
    require(game.ammo == 0 && game.reserveAmmo == 0, "empty ammunition underflowed");

    const int station = game.radioStation;
    mc::Input radio;
    radio.radio = true;
    tick(game, radio);
    require(game.radioStation != station, "radio input did not change stations");
    const int newStation = game.radioStation;
    tick(game, radio, 60);
    require(game.radioStation == newStation, "holding radio cycled repeatedly");
}

void shoulderAimAndNearestHit() {
    mc::Game game;
    game.initialize();
    game.vehicles.clear();
    game.pedestrians.clear();
    game.player = {0, game.world.height(0, 0), 0};
    game.yaw = 0;
    game.pitch = 0;
    mc::Input aim;
    aim.aim = true;
    tick(game, aim);
    const mc::Vec3 eye = game.cameraEye();
    const mc::Vec3 sight = mc::normalized(game.cameraTarget() - eye);
    mc::Pedestrian nearTarget;
    nearTarget.position = eye + sight * 100 - mc::Vec3{0, 1, 0};
    mc::Pedestrian farTarget;
    farTarget.position = eye + sight * 130 - mc::Vec3{0, 1, 0};
    game.pedestrians.push_back(nearTarget);
    game.pedestrians.push_back(farTarget);
    const int ammunition = game.ammo;
    aim.fire = true;
    tick(game, aim);
    require(close(game.pedestrians[0].health, 60),
            "shoulder camera crosshair did not hit the centered target");
    require(close(game.pedestrians[1].health, 100),
            "one shot passed through the nearest target into a second target");
    require(game.ammo == ammunition - 1, "aimed shot consumed incorrect ammunition");
}

void policeObstruction() {
    mc::Game game;
    game.initialize();
    game.vehicles.clear();
    game.pedestrians.clear();
    const float ground = game.world.height(0, 0);
    game.player = {0, ground, 0};
    game.health = 80;
    game.wanted = 1;
    mc::Pedestrian officer;
    officer.position = {0, game.world.height(0, 10), 10};
    game.pedestrians.push_back(officer);
    game.world.chunks.front().solids.push_back({{-5, ground - 1, 4}, {5, ground + 3, 6}});
    tick(game);
    require(close(game.health, 80), "officer dealt damage through a solid wall");
    game.world.chunks.front().solids.pop_back();
    game.wanted = 1;
    tick(game);
    require(game.health < 80, "visible nearby officer did not engage the wanted player");

    game.health = 80;
    game.wanted = 1;
    game.pedestrians.clear();
    mc::Vehicle patrol;
    patrol.position = {0, game.world.height(0, 4), 4};
    patrol.police = true;
    game.vehicles.push_back(patrol);
    game.world.chunks.front().solids.push_back({{-5, ground - 1, 1}, {5, ground + 3, 2}});
    tick(game);
    require(close(game.health, 80), "patrol vehicle dealt damage through a solid wall");
    game.world.chunks.front().solids.pop_back();
    game.wanted = 1;
    tick(game);
    require(game.health < 80, "visible patrol vehicle did not engage the wanted player");
}

void verifyPoliceDetectionAltitude(bool onFoot) {
    for (float altitude : {120.0f, 20.0f}) {
        mc::Game game;
        const float ground = game.world.height(0, 0);
        mc::Vehicle aircraft;
        aircraft.kind = mc::VehicleKind::Aircraft;
        aircraft.position = {0, ground + altitude, 0};
        game.vehicles.push_back(aircraft);
        game.player = aircraft.position;
        game.occupied = 0;
        game.health = 80;
        game.wanted = 1;
        if (onFoot) {
            mc::Pedestrian officer;
            officer.position = {0, ground, 0};
            game.pedestrians.push_back(officer);
        } else {
            mc::Vehicle patrol;
            patrol.position = {0, ground, 0};
            patrol.police = true;
            patrol.parked = true;
            game.vehicles.push_back(patrol);
        }
        tick(game);
        require(close(game.health, 80), "ground police damaged an aircraft out of weapon range");
        if (altitude > 100) {
            require(game.wanted == 0, onFoot
                ? "ground officer maintained detection beyond vertical sight range"
                : "ground patrol maintained detection beyond vertical sight range");
        } else {
            require(game.wanted == 1, onFoot
                ? "nearby officer failed to retain a clearly visible suspect"
                : "nearby patrol failed to retain a clearly visible suspect");
        }
    }
}

void patrolDetectionUsesAltitude() { verifyPoliceDetectionAltitude(false); }
void officerDetectionUsesAltitude() { verifyPoliceDetectionAltitude(true); }

void campaignProgression() {
    mc::Game game;
    game.initialize();
    game.vehicles.clear();
    game.pedestrians.clear();
    require(mc::Game::missions().size() == 4, "campaign mission count changed");
    const auto place = [&](mc::Vec3 destination) {
        destination.y = game.world.height(destination.x, destination.z);
        game.player = destination;
        if (game.occupied >= 0) {
            mc::Vehicle& vehicle = game.vehicles[static_cast<size_t>(game.occupied)];
            vehicle.position = destination;
            vehicle.speed = 0;
            vehicle.velocity = {};
        }
    };
    const auto start = [&](int expectedMission) {
        require(game.activeMission == -1, "previous mission remained active");
        require(game.completedMissions == expectedMission, "campaign completion is out of order");
        require(mc::length(game.missionTarget() - mc::Game::missions()[expectedMission].start) < 0.01f,
                "inactive mission target does not point to the next contact");
        place(game.missionTarget());
        tick(game);
        mc::Input accept;
        accept.mission = true;
        tick(game, accept);
        require(game.activeMission == expectedMission, "contact did not start the next mission");
        tick(game);
    };
    int expectedMoney = game.money;
    start(0);
    require(game.missionStage == 0, "first mission skipped the vehicle requirement");
    mc::Vehicle car;
    car.position = game.player + mc::Vec3{1, 0, 0};
    car.parked = true;
    game.vehicles.push_back(car);
    mc::Input enter;
    enter.interact = true;
    tick(game, enter);
    require(game.occupied == 0 && game.missionStage == 1,
            "entering a car did not advance the first mission");
    place(game.missionTarget());
    tick(game);
    expectedMoney += mc::Game::missions()[0].reward;
    require(game.completedMissions == 1 && game.money == expectedMoney,
            "first mission completion or reward is incorrect");
    tick(game, {}, 3);
    require(game.money == expectedMoney, "completed mission awarded its reward more than once");

    start(1);
    require(game.missionTimer > 0, "delivery mission has no deadline");
    const mc::Vec3 pickup = game.missionTarget();
    place(pickup);
    tick(game);
    require(game.missionStage == 1 && game.completedMissions == 1,
            "delivery pickup did not advance to the drop-off objective");
    require(mc::length(game.missionTarget() - pickup) > 20,
            "delivery drop-off did not move to a new destination");
    place(game.missionTarget());
    tick(game);
    expectedMoney += mc::Game::missions()[1].reward;
    require(game.completedMissions == 2 && game.money == expectedMoney,
            "delivery completion or reward is incorrect");

    start(2);
    require(game.wanted > 0 && game.missionStage == 1,
            "getaway mission did not begin a police pursuit");
    place(game.missionTarget());
    tick(game);
    require(game.missionStage == 2 && game.completedMissions == 2,
            "getaway completed while police were still searching");
    game.wanted = 0;
    tick(game);
    expectedMoney += mc::Game::missions()[2].reward;
    require(game.completedMissions == 3 && game.money == expectedMoney,
            "getaway did not complete after losing the police");

    start(3);
    place(game.missionTarget());
    tick(game);
    require(game.missionStage == 0, "foot objective accepted an occupied vehicle");
    game.occupied = -1;
    tick(game);
    require(game.missionStage == 1, "first recording did not advance the foot objective");
    place(game.missionTarget());
    tick(game);
    expectedMoney += mc::Game::missions()[3].reward;
    require(game.completedMissions == 4 && game.activeMission == -1,
            "final mission did not complete the campaign");
    require(game.money == expectedMoney, "final mission reward is incorrect");
    require(game.missionInfo() == nullptr, "completed campaign still exposes an active contact");
}

void missionDeadline() {
    mc::Game game;
    game.initialize();
    game.completedMissions = 1;
    game.player = mc::Game::missions()[1].start;
    game.player.y = game.world.height(game.player.x, game.player.z);
    const int initialMoney = game.money;
    mc::Input accept;
    accept.mission = true;
    tick(game, accept);
    require(game.activeMission == 1, "delivery deadline test could not start mission");
    game.missionTimer = 0.001f;
    tick(game);
    require(game.activeMission == -1 && game.completedMissions == 1,
            "expired delivery was not returned to its contact");
    require(game.money == initialMoney, "expired delivery awarded money");
    tick(game, accept);
    require(game.activeMission == 1 && game.missionTimer > 100,
            "expired delivery could not be retried");
}

struct TemporarySave {
    std::filesystem::path path;
    TemporarySave() {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("meridian-game-test-" + std::to_string(stamp) + ".sav");
    }
    ~TemporarySave() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(bool(stream), "could not open temporary save file");
    if (!bytes.empty()) stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(bool(stream), "could not write temporary save file");
}

void setLittleEndian(std::vector<char>& bytes, size_t offset, uint32_t value) {
    require(offset + 4 <= bytes.size(), "save fixture offset exceeds file size");
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset++] = static_cast<char>(value >> shift);
}

uint32_t littleEndian(const std::vector<char>& bytes, size_t offset) {
    require(offset + 4 <= bytes.size(), "save fixture read exceeds file size");
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= uint32_t(static_cast<unsigned char>(bytes[offset++])) << shift;
    return value;
}

void refreshSaveChecksum(std::vector<char>& bytes) {
    require(bytes.size() >= 20, "save fixture has no complete header");
    uint32_t checksum = 0xffffffffu;
    for (size_t i = 20; i < bytes.size(); ++i) {
        checksum ^= static_cast<unsigned char>(bytes[i]);
        for (int bit = 0; bit < 8; ++bit)
            checksum = (checksum >> 1) ^ ((0u - (checksum & 1u)) & 0xedb88320u);
    }
    setLittleEndian(bytes, 16, ~checksum);
}

void saveRoundTripAndCorruption() {
    mc::Game game;
    game.initialize();
    game.player = {19.25f, game.world.height(19.25f, 6.5f), 6.5f};
    game.health = 73.5f;
    game.money = 12345;
    game.ammo = 11;
    game.reserveAmmo = 67;
    game.completedMissions = 2;
    game.activeMission = 2;
    game.missionStage = 1;
    game.missionTimer = 55.5f;
    game.wanted = 3;
    game.radioStation = 3;
    game.time = 1234.25f;
    game.dayTime = 21.25f;
    game.rain = 0.6f;
    game.vehicles[0].health = 34.5f;
    game.vehicles[0].color = {0.11f, 0.22f, 0.33f};
    game.vehicles[0].pitch = 0.12f;
    game.vehicles[0].roll = -0.31f;
    game.vehicles[0].throttle = 0.65f;
    game.pedestrians[0].health = 42;
    game.pedestrians[0].panic = 8;
    const size_t vehicleCount = game.vehicles.size();
    const size_t pedestrianCount = game.pedestrians.size();
    const mc::Vec3 savedPosition = game.player;
    TemporarySave save;
    require(game.save(save.path.string()), "saving failed");
    std::ifstream input(save.path, std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(input)), {});
    input.close();
    require(original.size() > 16, "save file is unexpectedly short");

    game.player = {-500, 80, -500};
    game.health = 1;
    game.money = 0;
    game.ammo = game.reserveAmmo = game.completedMissions = 0;
    game.activeMission = -1;
    game.missionStage = 0;
    game.missionTimer = 0;
    game.wanted = 0;
    game.radioStation = 0;
    game.time = 0;
    game.dayTime = 0;
    game.rain = 0;
    game.vehicles.clear();
    game.pedestrians.clear();
    require(game.load(save.path.string()), "loading a valid save failed");
    require(mc::length(game.player - savedPosition) < 0.01f, "save did not restore position");
    require(close(game.health, 73.5f), "save did not restore health");
    require(game.money == 12345, "save did not restore money");
    require(game.ammo == 11 && game.reserveAmmo == 67, "save did not restore ammunition");
    require(game.completedMissions == 2, "save did not restore campaign progress");
    require(game.activeMission == 2 && game.missionStage == 1 && close(game.missionTimer, 55.5f),
            "save did not restore an active mission and its deadline");
    require(game.wanted == 3 && game.radioStation == 3, "save did not restore wanted/radio state");
    require(close(game.time, 1234.25f) && close(game.rain, 0.6f),
            "save did not restore simulation time and weather");
    require(close(game.dayTime, 21.25f), "save did not restore time of day");
    require(game.vehicles.size() == vehicleCount && game.pedestrians.size() == pedestrianCount,
            "save did not restore dynamic entity counts");
    require(close(game.vehicles[0].health, 34.5f) &&
            mc::length(game.vehicles[0].color - mc::Vec3{0.11f, 0.22f, 0.33f}) < 0.001f,
            "save did not restore vehicle condition and appearance");
    require(close(game.vehicles[0].pitch, 0.12f) && close(game.vehicles[0].roll, -0.31f) &&
            close(game.vehicles[0].throttle, 0.65f),
            "save did not restore vehicle pitch, roll, and throttle");
    require(close(game.pedestrians[0].health, 42) && close(game.pedestrians[0].panic, 8),
            "save did not restore pedestrian condition");

    const auto reject = [&](std::vector<char> bytes) {
        writeBytes(save.path, bytes);
        require(!game.load(save.path.string()), "invalid save was accepted");
        require(game.money == 12345 && game.completedMissions == 2,
                "failed load partially overwrote campaign state");
        require(mc::length(game.player - savedPosition) < 0.01f,
                "failed load partially overwrote position");
    };
    reject({});
    reject(std::vector<char>(original.begin(), original.begin() + original.size() / 2));
    reject(std::vector<char>(original.begin(), original.end() - 1));
    std::vector<char> corrupted = original;
    corrupted[0] ^= 0x40;
    reject(corrupted);
    corrupted = original;
    corrupted[corrupted.size() / 2] ^= 0x40;
    reject(corrupted);
    // These fixtures carry valid checksums: the parser must validate their content too.
    for (uint32_t invalidCoordinate : {0x7fc00000u, 0x7f800000u, 0x47000000u}) {
        corrupted = original;
        setLittleEndian(corrupted, 20, invalidCoordinate);
        refreshSaveChecksum(corrupted);
        reject(corrupted);
    }
    corrupted = original;
    setLittleEndian(corrupted, 56, 6); // Shared wanted-level field, beyond its legal maximum.
    refreshSaveChecksum(corrupted);
    reject(corrupted);
    corrupted = original;
    setLittleEndian(corrupted, 112, 0xffffffffu); // Shared vehicle-count field.
    refreshSaveChecksum(corrupted);
    reject(corrupted);
    corrupted = original;
    setLittleEndian(corrupted, 180, 0x7fc00000u); // Version 2 first vehicle pitch: NaN.
    refreshSaveChecksum(corrupted);
    reject(corrupted);
    corrupted = original;
    setLittleEndian(corrupted, 188, 0x40000000u); // Version 2 first vehicle throttle: 2.0.
    refreshSaveChecksum(corrupted);
    reject(corrupted);
    corrupted = original;
    corrupted.push_back(0);
    reject(corrupted);
    require(!game.load(save.path.string() + ".absent"), "missing save file was accepted");
    require(!game.save(save.path.string() + "/file.sav"),
            "save reported success for an invalid directory");
}

void legacySaveMigration() {
    mc::Game game;
    game.initialize();
    game.vehicles.erase(std::remove_if(game.vehicles.begin(), game.vehicles.end(),
        [](const mc::Vehicle& vehicle) {
            return vehicle.kind == mc::VehicleKind::Boat || vehicle.kind == mc::VehicleKind::Aircraft;
        }), game.vehicles.end());
    const size_t legacyVehicleCount = game.vehicles.size();
    game.money = 9876;
    game.occupied = 0;
    game.player = game.vehicles[0].position;
    game.vehicles[0].pitch = 0.12f;
    game.vehicles[0].roll = -0.25f;
    game.vehicles[0].throttle = 0.6f;
    TemporarySave save;
    require(game.save(save.path.string()), "could not write migration fixture");
    std::ifstream input(save.path, std::ios::binary);
    const std::vector<char> current((std::istreambuf_iterator<char>(input)), {});
    input.close();
    require(littleEndian(current, 8) == 2, "new saves do not use version 2");
    require(littleEndian(current, 112) == legacyVehicleCount,
            "migration fixture has an unexpected vehicle count");
    constexpr size_t vehicleStart = 116, currentStride = 76, legacyStride = 64;
    const size_t tail = vehicleStart + legacyVehicleCount * currentStride;
    require(tail <= current.size(), "migration fixture has truncated vehicle records");
    std::vector<char> legacy(current.begin(), current.begin() + vehicleStart);
    for (size_t i = 0; i < legacyVehicleCount; ++i) {
        const size_t start = vehicleStart + i * currentStride;
        legacy.insert(legacy.end(), current.begin() + start, current.begin() + start + legacyStride);
    }
    legacy.insert(legacy.end(), current.begin() + tail, current.end());
    setLittleEndian(legacy, 8, 1);
    setLittleEndian(legacy, 12, static_cast<uint32_t>(legacy.size() - 20));
    refreshSaveChecksum(legacy);
    writeBytes(save.path, legacy);
    game.money = 0;
    game.vehicles.clear();
    require(game.load(save.path.string()), "valid version 1 save was rejected");
    require(game.money == 9876 && game.occupied == 0,
            "legacy migration lost progress or the occupied vehicle");
    require(game.vehicles.size() == legacyVehicleCount + 2,
            "legacy migration did not add missing starter craft");
    for (size_t i = 0; i < legacyVehicleCount; ++i)
        require(close(game.vehicles[i].pitch, 0) && close(game.vehicles[i].roll, 0) &&
                close(game.vehicles[i].throttle, 0), "legacy vehicle fields were not initialized to zero");
    const auto countKind = [&](mc::VehicleKind kind) {
        return std::count_if(game.vehicles.begin(), game.vehicles.end(),
            [kind](const mc::Vehicle& vehicle) { return vehicle.kind == kind; });
    };
    require(countKind(mc::VehicleKind::Boat) == 1 && countKind(mc::VehicleKind::Aircraft) == 1,
            "legacy migration duplicated or omitted a starter craft");
    require(game.save(save.path.string()) && game.load(save.path.string()),
            "migrated save could not be saved and reloaded as version 2");
    require(game.vehicles.size() == legacyVehicleCount + 2,
            "reloading a migrated save duplicated starter craft");
}

void unoccupiedAircraftMotionAndPersistence() {
    mc::Game game;
    const float ground = game.world.height(-3200, -1000);
    game.player = {-3200, ground, -1000};
    mc::Vehicle aircraft;
    aircraft.kind = mc::VehicleKind::Aircraft;
    aircraft.position = {-3200, ground + 100, -1000};
    aircraft.speed = 28;
    aircraft.velocity = {0, 0, 28};
    aircraft.throttle = 0.8f;
    game.vehicles.push_back(aircraft);
    tick(game, {}, 180);
    require(game.occupied == -1 && close(game.vehicles[0].throttle, 0),
            "unoccupied aircraft did not cut engine power");
    require(game.vehicles[0].position.y < aircraft.position.y - 0.5f &&
            mc::length(game.vehicles[0].position - aircraft.position) > 1,
            "unoccupied aircraft froze instead of descending and gliding");
    const mc::Vec3 savedPosition = game.vehicles[0].position;
    const mc::Vec3 savedVelocity = game.vehicles[0].velocity;
    TemporarySave save;
    require(game.save(save.path.string()), "could not save an unoccupied airborne aircraft");
    mc::Game restored;
    require(restored.load(save.path.string()), "could not load an unoccupied airborne aircraft");
    require(mc::length(restored.vehicles[0].position - savedPosition) < 0.001f &&
            mc::length(restored.vehicles[0].velocity - savedVelocity) < 0.001f,
            "save/load changed unoccupied aircraft motion");
    tick(restored, {}, 60);
    require(restored.vehicles[0].position.y < savedPosition.y,
            "restored unoccupied aircraft stopped descending");

    mc::Vehicle& atBoundary = restored.vehicles[0];
    atBoundary.position = {mc::World::Extent - 2.1f, 120, mc::World::Extent - 2.1f};
    atBoundary.yaw = mc::Pi * 0.25f;
    atBoundary.speed = 50;
    atBoundary.velocity = mc::forward(atBoundary.yaw) * 50;
    atBoundary.pitch = atBoundary.roll = 0;
    tick(restored, {}, 60);
    require(finite(atBoundary.position) && finite(atBoundary.velocity),
            "unoccupied boundary aircraft produced nonfinite motion");
    require(std::fabs(atBoundary.position.x) <= mc::World::Extent - 2 &&
            std::fabs(atBoundary.position.z) <= mc::World::Extent - 2 &&
            atBoundary.position.y >= -100 && atBoundary.position.y <= 4096,
            "unoccupied aircraft escaped the saveable world bounds");
    require(restored.save(save.path.string()) && game.load(save.path.string()),
            "boundary aircraft state could not be saved and restored");

    game = mc::Game{};
    game.player = {-3200, ground, -1000};
    aircraft = mc::Vehicle{};
    aircraft.kind = mc::VehicleKind::Aircraft;
    aircraft.position = game.player;
    aircraft.parked = true;
    aircraft.throttle = 0.8f;
    game.vehicles.push_back(aircraft);
    tick(game, {}, 240);
    require(mc::length(game.vehicles[0].position - aircraft.position) < 0.001f &&
            close(game.vehicles[0].speed, 0) && close(game.vehicles[0].throttle, 0),
            "unoccupied parked aircraft drifted or accelerated on the ground");
}

void simulationSmoke() {
    mc::Game game;
    game.initialize();
    for (int frame = 0; frame < 1800; ++frame) {
        mc::Input input;
        input.moveY = frame % 240 < 200 ? 1.0f : -0.5f;
        input.moveX = frame % 360 < 180 ? 0.4f : -0.4f;
        input.lookX = 0.001f;
        input.sprint = frame % 120 < 60;
        input.fire = frame % 90 < 12;
        input.reload = frame % 180 == 150;
        input.jump = frame % 240 == 120;
        input.radio = frame % 300 == 0;
        tick(game, input);
        require(finite(game.player), "simulation produced a nonfinite player position");
        require(finite(game.cameraEye()) && finite(game.cameraTarget()),
                "simulation produced a nonfinite camera");
        require(std::isfinite(game.health) && game.health >= 0 && game.health <= 100,
                "simulation produced invalid health");
        require(game.ammo >= 0 && game.reserveAmmo >= 0,
                "simulation produced negative ammunition");
        require(game.wanted >= 0 && game.wanted <= 5, "simulation produced invalid wanted level");
        require(game.occupied == -1 || (game.occupied >= 0 &&
                static_cast<size_t>(game.occupied) < game.vehicles.size()),
                "simulation left an invalid vehicle reference");
        for (const mc::Vehicle& vehicle : game.vehicles)
            require(finite(vehicle.position) && finite(vehicle.velocity) &&
                    std::isfinite(vehicle.yaw) && std::isfinite(vehicle.speed),
                    "simulation produced invalid vehicle state");
        for (const mc::Pedestrian& pedestrian : game.pedestrians)
            require(finite(pedestrian.position) && std::isfinite(pedestrian.health),
                    "simulation produced invalid pedestrian state");
    }
    verifyMesh(game.dynamicMesh());
}
} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"initialization and geometry", initializationAndGeometry},
        {"lighting activation and vehicle transforms", lightingActivationAndTransforms},
        {"lighting capacity, range, and order", lightingCapacityRangeAndOrder},
        {"movement, pause, and timestep bounds", movementAndPause},
        {"vehicle interaction", vehicleInteraction},
        {"boat handling and swimming", boatHandlingAndSwimming},
        {"aircraft flight, stall, and landing", aircraftFlightAndLanding},
        {"hard aircraft landings across frame offsets", hardAircraftLandingsAcrossFrameOffsets},
        {"aircraft altitude separates road contacts", aircraftAltitudeSeparatesContacts},
        {"ditched aircraft exit and swimming", ditchedAircraftExitAndSwimming},
        {"aircraft exit prefers a clear dock", aircraftExitPrefersDock},
        {"weapons and radio", weaponsAndRadio},
        {"shoulder aim and nearest hit", shoulderAimAndNearestHit},
        {"police damage respects solid walls", policeObstruction},
        {"patrol detection uses altitude", patrolDetectionUsesAltitude},
        {"officer detection uses altitude", officerDetectionUsesAltitude},
        {"complete campaign progression", campaignProgression},
        {"delivery deadline and retry", missionDeadline},
        {"save round trip and corruption", saveRoundTripAndCorruption},
        {"version 1 save migration", legacySaveMigration},
        {"unoccupied aircraft motion and persistence", unoccupiedAircraftMotionAndPersistence},
        {"thirty second simulation smoke", simulationSmoke},
    };
    int failures = 0;
    for (const Test& test : tests) {
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
            ++failures;
        }
    }
    std::cout << (sizeof(tests) / sizeof(tests[0])) - failures << "/"
              << sizeof(tests) / sizeof(tests[0]) << " game tests passed\n";
    return failures == 0 ? 0 : 1;
}
