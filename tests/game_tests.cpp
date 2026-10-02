#include "../src/game.h"
#include "../src/audio_scene.h"

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

void movementSoundMapping() {
    mc::Game game;game.initialize();mc::AudioState sound;mc::Input input;
    constexpr float dt=1.f/60;
    auto before=game.player;input.moveY=1;game.update(input,dt);
    mc::movementAudio(sound,game,before,-1,input,dt);
    require(sound.footContact&&sound.footSpeed>3&&sound.footSpeed<5,"walking did not produce grounded movement sound");
    mc::movementAudio(sound,game,game.player,-1,input,dt);
    require(sound.footSpeed==0,"walking input without displacement produced footsteps");
    before=game.player;game.player.y+=2;game.player.z+=.05f;
    mc::movementAudio(sound,game,before,-1,input,dt);
    require(!sound.footContact,"airborne movement produced grounded contact");
    game.player={2674,game.world.height(2674,768),768};before=game.player;before.x-=.04f;
    mc::movementAudio(sound,game,before,-1,input,dt);
    require(sound.footContact&&sound.footSurface==mc::FootSurface::Wood,"pier footsteps ignored its wood surface");
    game.player={3000,mc::World::WaterLevel-1.1f,900};before=game.player;before.x-=.04f;
    mc::movementAudio(sound,game,before,-1,input,dt);
    require(!sound.footContact&&sound.footSpeed==0&&sound.waterMotion>.5f,"swimming did not replace dry contact with water motion");
    game.player={8,game.world.height(8,8),8};before=game.player;before.x-=100;
    mc::movementAudio(sound,game,before,-1,input,dt);
    require(sound.footSpeed==0&&sound.waterMotion==0,"teleport generated movement sound");
    before=game.player;before.x-=.04f;
    mc::movementAudio(sound,game,before,0,input,dt);
    require(!sound.footContact&&sound.footSpeed==0,"vehicle exit generated a false stride");
    game.occupied=0;auto& vehicle=game.vehicles[0];vehicle.kind=mc::VehicleKind::Car;vehicle.yaw=0;vehicle.speed=20;vehicle.velocity={5,0,20};
    mc::movementAudio(sound,game,before,0,input,dt);
    require(sound.tireScrub>.5f&&!sound.footContact,"lateral tire slip did not produce scrub");
    vehicle.velocity={0,0,20};input.brake=true;
    mc::movementAudio(sound,game,before,0,input,dt);
    require(sound.tireScrub>0,"braking a moving car did not produce scrub");
    vehicle.kind=mc::VehicleKind::Aircraft;
    mc::movementAudio(sound,game,before,0,input,dt);
    require(sound.tireScrub==0,"aircraft controls produced road tire scrub");
    vehicle.kind=mc::VehicleKind::Car;game.paused=true;
    mc::movementAudio(sound,game,before,0,input,dt);
    require(sound.tireScrub==0&&sound.waterMotion==0&&sound.footSpeed==0,"paused simulation retained movement sounds");
    game.paused=false;
    mc::movementAudio(sound,game,before,0,input,0);
    require(sound.tireScrub==0&&sound.footSpeed==0,"zero timestep retained movement sounds");
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

    const uint64_t residentRevision = game.world.revision;
    std::vector<int> residentCoordinates;
    for (const mc::Chunk& chunk : game.world.chunks) {
        residentCoordinates.push_back(chunk.x);
        residentCoordinates.push_back(chunk.z);
    }
    game.player = {-3200, game.world.height(-3200, -1000), -1000};
    const mc::Vec3 remoteStart = game.player;
    game.update(walk, 1.0f / 60.0f, false);
    require(mc::length(game.player - remoteStart) > 0.01f,
            "disabling synchronous streaming also disabled player movement");
    require(game.world.revision == residentRevision &&
            game.world.chunks.size() * 2 == residentCoordinates.size(),
            "simulation replaced resident chunks while synchronous streaming was disabled");
    for (size_t i = 0; i < game.world.chunks.size(); ++i)
        require(game.world.chunks[i].x == residentCoordinates[i * 2] &&
                game.world.chunks[i].z == residentCoordinates[i * 2 + 1],
                "simulation changed chunk coordinates while synchronous streaming was disabled");

    game.update(walk, 1.0f / 60.0f);
    const int centerX = int(std::floor(game.player.x / mc::World::ChunkSize));
    const int centerZ = int(std::floor(game.player.z / mc::World::ChunkSize));
    require(game.world.revision > residentRevision &&
            std::any_of(game.world.chunks.begin(), game.world.chunks.end(),
                [&](const mc::Chunk& chunk) { return chunk.x == centerX && chunk.z == centerZ; }),
            "default simulation update did not stream the new player location");
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

void vehicleBoardingRespectsWalls() {
    mc::Game game;
    const float ground = game.world.height(0, 0);
    mc::Vehicle hidden;
    hidden.position = {0, ground, 0};
    hidden.parked = true;
    game.vehicles.push_back(hidden);
    game.player = {4, ground, 0};
    game.health = 80;
    game.money = 1234;
    game.world.stream(game.player);
    game.world.chunks.front().solids.push_back({{1.8f, ground - 1, -10}, {2.2f, ground + 3, 10}});
    mc::Input enter;
    enter.interact = true;
    tick(game, enter);
    require(game.occupied == -1 && mc::length(game.player - mc::Vec3{4, ground, 0}) < .001f,
            "interact boarded a nearby vehicle through a solid wall");
    require(close(game.health, 80) && game.money == 1234,
            "rejected vehicle entry changed health or money");

    mc::Vehicle reachable = hidden;
    reachable.position = {4, ground, 5};
    game.vehicles.push_back(reachable);
    tick(game);
    tick(game, enter);
    require(game.occupied == 1,
            "an obstructed nearer vehicle prevented entry to the reachable vehicle");

    mc::Game belowCeiling;
    belowCeiling.player = {0, ground, 0};
    hidden.position = {0, ground + 2.8f, 0};
    belowCeiling.vehicles.push_back(hidden);
    belowCeiling.world.stream(belowCeiling.player);
    belowCeiling.world.chunks.front().solids.push_back(
        {{-4, ground + 1.8f, -4}, {4, ground + 2, 4}});
    require(!belowCeiling.world.blocked(belowCeiling.player, .34f),
            "ceiling entry fixture obstructs the standing player");
    tick(belowCeiling, enter);
    require(belowCeiling.occupied == -1 && close(belowCeiling.player.y, ground),
            "interact boarded an elevated vehicle through a solid ceiling");

    mc::Game lowBeam;
    lowBeam.player = {0, ground, 0};
    hidden.position = {0, ground, 4};
    lowBeam.vehicles.push_back(hidden);
    lowBeam.world.stream(lowBeam.player);
    lowBeam.world.chunks.front().solids.push_back(
        {{-2, ground + 1.67f, 1.8f}, {2, ground + 2, 2.2f}});
    require(!lowBeam.world.blocked(lowBeam.player, .34f) &&
            !lowBeam.world.blocked(hidden.position, .34f) &&
            lowBeam.world.blocked({0, ground, 2}, .34f),
            "low-beam boarding fixture does not obstruct only the middle of the path");
    tick(lowBeam, enter);
    require(lowBeam.occupied == -1 && mc::length(lowBeam.player - mc::Vec3{0, ground, 0}) < .001f,
            "interact moved the player's head through a low beam between clear endpoints");
}

void vehicleExitRequiresClearPath() {
    const auto prepare = [] {
        mc::Game game;
        mc::Vehicle car;
        car.position = {0, game.world.height(0, 0), 0};
        car.parked = true;
        game.vehicles.push_back(car);
        game.player = car.position;
        game.occupied = 0;
        game.health = 80;
        game.money = 1234;
        game.world.stream(game.player);
        return game;
    };
    mc::Game game = prepare();
    const mc::Vec3 origin = game.player;
    game.world.chunks.front().solids.push_back(
        {{-1.5f, origin.y - 1, -4}, {-1.3f, origin.y + 3, 4}});
    require(!game.world.blocked(origin + mc::Vec3{-2, 0, 0}, .35f),
            "exit fixture does not leave a clear endpoint behind its wall");
    mc::Input exit;
    exit.interact = true;
    tick(game, exit);
    require(game.occupied == -1 && game.player.x > origin.x,
            "vehicle exit crossed a wall instead of choosing its clear opposite side");
    require(mc::length(game.world.move(origin, game.player - origin, .35f) - game.player) < .01f,
            "selected exit cannot be reached by the player's collision sweep");

    game = prepare();
    auto& solids = game.world.chunks.front().solids;
    solids.push_back({{-1.5f, origin.y - 1, -4}, {-1.3f, origin.y + 3, 4}});
    solids.push_back({{1.3f, origin.y - 1, -4}, {1.5f, origin.y + 3, 4}});
    solids.push_back({{-4, origin.y - 1, -2.4f}, {4, origin.y + 3, -2.2f}});
    solids.push_back({{-4, origin.y - 1, 2.2f}, {4, origin.y + 3, 2.4f}});
    tick(game, exit);
    require(game.occupied == 0 && mc::length(game.player - origin) < .001f &&
            close(game.health, 80) && game.money == 1234,
            "a fully enclosed vehicle allowed an exit through its surrounding walls");
}

mc::Game workshopFixture() {
    mc::Game game;
    const mc::GarageSite site = mc::World::garageSite();
    mc::Vehicle car;
    car.position = site.vehicleStop;
    car.yaw = site.vehicleHeading;
    car.parked = true;
    car.health = 40;
    game.vehicles.push_back(car);
    game.player = car.position;
    game.occupied = 0;
    game.health = 80;
    game.money = 1000;
    game.world.stream(game.player);
    return game;
}

void workshopBayRepairs() {
    const mc::Game base = workshopFixture();
    mc::Input service;
    service.mission = true;
    for (mc::VehicleKind kind : {mc::VehicleKind::Car, mc::VehicleKind::Motorcycle}) {
        mc::Game game = base;
        game.vehicles[0].kind = kind;
        require(!game.world.blocked(game.player, kind == mc::VehicleKind::Car ? 1.02f : .45f),
                "workshop service pad is obstructed");
        const char* instruction = game.workshopInstruction();
        require(instruction && std::string(instruction).find("$75") != std::string::npos,
                "stopped damaged road vehicle did not receive the repair price");
        tick(game, service);
        require(close(game.vehicles[0].health, 100) && game.money == 925,
                "bay service did not repair the road vehicle for exactly $75");
        require(close(game.health, 80) && game.occupied == 0,
                "vehicle repair changed the driver's health or occupancy");
        game.vehicles[0].health = 70;
        tick(game, service, 3);
        require(close(game.vehicles[0].health, 70) && game.money == 925,
                "holding the service button bought another repair");
        tick(game);
        tick(game, service);
        require(close(game.vehicles[0].health, 100) && game.money == 850,
                "a newly pressed service button could not buy a second needed repair");
        tick(game);
        tick(game, service);
        require(game.money == 850 && close(game.health, 80),
                "a fully repaired road vehicle was charged again");
    }
}

void workshopServiceBoundaries() {
    const mc::Game base = workshopFixture();
    const mc::GarageSite site = mc::World::garageSite();
    mc::Input service;
    service.mission = true;
    const auto reject = [&](mc::Game game, const char* reason) {
        const int money = game.money;
        tick(game, service);
        require(game.money == money && game.vehicles[0].health < 99,
                reason);
        require(close(game.health, 80), "rejected workshop repair healed the driver");
    };
    mc::Game game = base;
    game.money = 74;
    reject(game, "repair ignored insufficient funds");
    game = base;
    game.wanted = 3;
    reject(game, "repair was sold during pursuit");
    game = base;
    game.vehicles[0].speed = 4;
    game.vehicles[0].velocity = mc::forward(game.vehicles[0].yaw) * 4;
    reject(game, "repair was sold to a moving road vehicle");
    game = base;
    game.vehicles[0].velocity = {1, 0, 0};
    reject(game, "repair ignored lateral vehicle motion");
    for (mc::VehicleKind kind : {mc::VehicleKind::Boat, mc::VehicleKind::Aircraft}) {
        game = base;
        game.vehicles[0].kind = kind;
        reject(game, "workshop repaired an unsupported water or air vehicle");
    }
    game = base;
    game.player = game.vehicles[0].position = site.marker;
    reject(game, "outside workshop marker sold a repair without entering the bay");
    game = base;
    game.player = game.vehicles[0].position = {140, game.world.height(140, 128), 128};
    reject(game, "obsolete outdoor garage bubble still sold a repair");
    game = base;
    game.occupied = -1;
    game.player = site.counter;
    game.player.y += 1;
    const int before = game.money;
    tick(game, service);
    require(game.money == before && close(game.health, 80),
            "counter supplied first aid to a player above its service floor");
}

void workshopCounterFirstAid() {
    mc::Game base = workshopFixture();
    const mc::GarageSite site = mc::World::garageSite();
    base.occupied = -1;
    base.player = site.counter;
    require(!base.world.blocked(base.player, .34f), "workshop customer position is obstructed");
    mc::Input service;
    service.mission = true;
    mc::Game game = base;
    const char* instruction = game.workshopInstruction();
    require(instruction && std::string(instruction).find("$25") != std::string::npos,
            "injured customer did not receive the first-aid price");
    tick(game, service);
    require(close(game.health, 100) && game.money == 975 && close(game.vehicles[0].health, 40),
            "counter did not heal only the customer for exactly $25");
    game.health = 80;
    tick(game, service, 3);
    require(close(game.health, 80) && game.money == 975,
            "holding the service button bought additional first aid");
    tick(game);
    tick(game, service);
    require(close(game.health, 100) && game.money == 950,
            "new first-aid purchase failed after releasing the button");
    tick(game);
    tick(game, service);
    require(game.money == 950, "healthy customer was charged for first aid");
    for (int scenario = 0; scenario < 5; ++scenario) {
        game = base;
        if (scenario == 0) game.money = 24;
        if (scenario == 1) game.wanted = 3;
        if (scenario == 2) game.player = site.vehicleStop;
        if (scenario == 3) game.player = site.marker;
        if (scenario == 4) game.player = {140, game.world.height(140, 128), 128};
        const int money = game.money;
        tick(game, service);
        require(game.money == money && close(game.health, 80) && close(game.vehicles[0].health, 40),
                "first aid ignored funds, pursuit, or the customer-area boundary");
    }
}

void workshopPopulatedServiceVisit() {
    mc::Game game;
    game.initialize();
    const mc::GarageSite site = mc::World::garageSite();
    const size_t population = game.pedestrians.size(), traffic = game.vehicles.size();
    mc::Vehicle& car = game.vehicles[0];
    car.position = site.streetAccess;
    car.yaw = site.vehicleHeading;
    car.speed = 0;
    car.velocity = {};
    car.health = 40;
    car.parked = false;
    game.player = car.position;
    game.yaw = car.yaw;
    game.occupied = 0;
    game.health = 80;
    game.money = 1000;
    bool stopped = false;
    int driveFrames = 0;
    for (; driveFrames < 1500; ++driveFrames) {
        const float distance = mc::dot(site.vehicleStop - car.position, mc::forward(site.vehicleHeading));
        if (std::fabs(distance) < 2 && std::fabs(car.speed) < .2f && mc::length(car.velocity) < .2f) {
            stopped = true;
            break;
        }
        const float desiredSpeed = mc::clamp(distance * .65f, 0.f, 5.f);
        mc::Input drive;
        drive.moveY = mc::clamp((desiredSpeed - car.speed) * .6f, 0.f, 1.f);
        drive.brake = distance < 1 || car.speed > desiredSpeed + .2f;
        tick(game, drive);
    }
    require(stopped && car.position.z >= site.serviceBay.min.z && car.position.z <= site.serviceBay.max.z,
            "control-driven car did not reach and stop inside the workshop bay");
    require(close(car.health, 40) && close(game.health, 80) && game.wanted == 0,
            "normal populated workshop approach caused damage or police pursuit");
    mc::Input service;
    service.mission = true;
    tick(game, service);
    require(close(car.health, 100) && game.money == 925 && close(game.health, 80),
            "driven workshop visit did not purchase the correct vehicle repair");
    mc::Input exit;
    exit.interact = true;
    tick(game, exit);
    require(game.occupied == -1 && !game.world.blocked(game.player, .34f),
            "repaired vehicle could not be exited safely inside the workshop");
    const mc::Vec3 route[] = {{game.player.x, site.floorHeight, site.shell.max.z + 2},
                              {site.pedestrianDoor.x, site.floorHeight, site.shell.max.z + 2},
                              site.counter};
    int walkFrames = 0;
    for (mc::Vec3 destination : route) {
        bool reached = false;
        for (int step = 0; step < 900; ++step) {
            mc::Vec3 delta = destination - game.player;
            delta.y = 0;
            if (mc::length(delta) < .15f) { reached = true; break; }
            mc::Input walk;
            const float turn = mc::wrapAngle(std::atan2(delta.x, delta.z) - game.yaw);
            walk.lookX = mc::clamp(turn, -1.f, 1.f);
            walk.moveY = std::fabs(turn) < .2f ? 1.f : 0.f;
            tick(game, walk);
            ++walkFrames;
        }
        require(reached, "player could not walk from the service bay through the office doorway");
    }
    tick(game, service);
    require(close(game.health, 100) && game.money == 900 && close(car.health, 100),
            "walked workshop visit did not purchase first aid at the counter");
    require(game.pedestrians.size() == population && game.vehicles.size() == traffic,
            "workshop visit replaced or cleared the initialized population");
    std::cout << "Populated workshop visit: " << driveFrames / 60.f << " s drive, "
              << walkFrames / 60.f << " s walk, repair and first aid $100\n";
}

void verifyCameraClearance(mc::Game& game, const char* fixture) {
    const mc::Vec3 anchor = game.player + mc::Vec3{0, game.occupied >= 0 ? 1.65f : 1.45f, 0};
    for (float pitch : {-.85f, -.4f, 0.f, .55f, 1.12f}) {
        game.pitch = pitch;
        for (int turn = 0; turn < 64; ++turn) {
            game.yaw = turn * (2 * mc::Pi / 64);
            const mc::Vec3 eye = game.cameraEye(), target = game.cameraTarget();
            require(finite(eye) && finite(target) && mc::length(eye - target) > 1,
                    "camera sweep produced an invalid view");
            require(eye.y >= game.world.height(eye.x, eye.z) + .34f,
                    "camera sweep placed the view below the ground");
            for (const mc::Chunk& chunk : game.world.chunks) for (const mc::Box& box : chunk.solids) {
                const mc::Vec3 nearest{
                    mc::clamp(eye.x, box.min.x, box.max.x),
                    mc::clamp(eye.y, box.min.y, box.max.y),
                    mc::clamp(eye.z, box.min.z, box.max.z)};
                if (mc::length(eye - nearest) < .10f)
                    throw std::runtime_error(std::string(fixture) + " camera touches solid geometry at yaw " +
                                             std::to_string(game.yaw) + " pitch " + std::to_string(pitch));
                // Test the whole view segment, since a clear endpoint can lie behind a thin wall.
                const mc::Vec3 delta = eye - anchor;
                const float p[] = {anchor.x, anchor.y, anchor.z};
                const float d[] = {delta.x, delta.y, delta.z};
                const float lo[] = {box.min.x, box.min.y, box.min.z};
                const float hi[] = {box.max.x, box.max.y, box.max.z};
                float enter = 0, leave = 1;
                bool crosses = true;
                for (int axis = 0; axis < 3; ++axis) {
                    if (std::fabs(d[axis]) < 1e-6f) {
                        if (p[axis] <= lo[axis] || p[axis] >= hi[axis]) crosses = false;
                    } else {
                        const float a = (lo[axis] - p[axis]) / d[axis];
                        const float b = (hi[axis] - p[axis]) / d[axis];
                        enter = std::max(enter, std::min(a, b));
                        leave = std::min(leave, std::max(a, b));
                        if (enter >= leave) crosses = false;
                    }
                }
                if (crosses)
                    throw std::runtime_error(std::string(fixture) + " camera crosses a solid wall at yaw " +
                                             std::to_string(game.yaw) + " pitch " + std::to_string(pitch));
            }
        }
    }
}

void cameraCloseWallAndWorkshopSweep() {
    mc::Game game;
    game.player = {0, game.world.height(0, 0), 0};
    game.world.stream(game.player);
    game.world.chunks.front().solids.push_back({{.36f, -1, -4}, {2, 5, 4}});
    game.world.chunks.front().solids.push_back({{-4, -1, .36f}, {4, 5, 2}});
    require(!game.world.blocked(game.player, .34f), "close-corner camera fixture obstructs the player");
    verifyCameraClearance(game, "close corner");
    game = workshopFixture();
    verifyCameraClearance(game, "occupied service bay");
    game.occupied = -1;
    const mc::GarageSite site = mc::World::garageSite();
    for (mc::Vec3 point : {site.counter, site.vehicleStop, site.pedestrianDoor, site.vehicleDoor,
                          mc::Vec3{156, site.floorHeight, 97.25f},
                          mc::Vec3{156, site.floorHeight, 98.75f}}) {
        game.player = point;
        require(!game.world.blocked(point, .34f), "workshop camera fixture obstructs the player");
        verifyCameraClearance(game, "workshop foot route");
    }
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
    require(mc::Game::missions().size() >= 6, "campaign is missing the two craft contracts");
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
            "fourth mission did not complete the road campaign");
    require(game.money == expectedMoney, "final mission reward is incorrect");
    require(game.missionInfo() == &mc::Game::missions()[4],
            "road campaign did not unlock the rescue contract");
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

void workshopInteriorSaveRoundTrip() {
    const mc::Game base = workshopFixture();
    const mc::GarageSite site = mc::World::garageSite();
    TemporarySave save;
    mc::Input service;
    service.mission = true;
    for (bool inVehicle : {false, true}) {
        mc::Game game = base;
        game.occupied = inVehicle ? 0 : -1;
        game.player = inVehicle ? site.vehicleStop : site.counter;
        game.harborSplit.bestTime = 80;
        game.harborSplit.medal = 3;
        require(game.save(save.path.string()), "saving inside the workshop failed");
        mc::Game restored;
        require(restored.load(save.path.string()), "loading inside the workshop failed");
        require(restored.occupied == game.occupied && mc::length(restored.player - game.player) < .001f &&
                close(restored.health, 80) && restored.money == 1000 &&
                restored.harborSplit.medal == 3 && close(restored.harborSplit.bestTime, 80),
                "interior save changed pose, occupancy, player state, or trial record");
        require(restored.vehicles.size() == 1 && close(restored.vehicles[0].health, 40) &&
                mc::length(restored.vehicles[0].position - site.vehicleStop) < .001f &&
                close(restored.vehicles[0].yaw, site.vehicleHeading),
                "interior save changed the parked service vehicle");
        tick(restored);
        require(mc::length(restored.player - game.player) < .001f &&
                close(restored.world.height(restored.player.x, restored.player.z), site.floorHeight) &&
                !restored.world.blocked(restored.player, inVehicle ? 1.02f : .34f),
                "loaded workshop occupant fell, moved, or became trapped on the next tick");
        require(restored.workshopInstruction() != nullptr,
                "loaded interior did not restore its service context");
        tick(restored, service);
        require(restored.money == (inVehicle ? 925 : 975) &&
                close(restored.health, inVehicle ? 80.f : 100.f) &&
                close(restored.vehicles[0].health, inVehicle ? 100.f : 40.f),
                "loaded workshop interior could not perform its correct service");
    }
}

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
    require(littleEndian(current, 8) == 4, "new saves do not use version 4");
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
    legacy.insert(legacy.end(), current.begin() + tail, current.end() - 16);
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
            "migrated save could not be saved and reloaded as version 4");
    require(game.vehicles.size() == legacyVehicleCount + 2,
            "reloading a migrated save duplicated starter craft");
}

void versionTwoSaveMigration() {
    mc::Game game;
    game.initialize();
    game.completedMissions = 4;
    game.money = 7654;
    game.vehicles[0].pitch = 0.14f;
    game.vehicles[0].roll = -0.2f;
    game.vehicles[0].throttle = 0.3f;
    const size_t vehicleCount = game.vehicles.size();
    TemporarySave save;
    require(game.save(save.path.string()), "could not create version 2 migration fixture");
    std::ifstream input(save.path, std::ios::binary);
    std::vector<char> legacy((std::istreambuf_iterator<char>(input)), {});
    input.close();
    require(littleEndian(legacy, 8) == 4, "version 2 fixture requires a version 4 source save");
    legacy.resize(legacy.size() - 16);
    setLittleEndian(legacy, 8, 2);
    setLittleEndian(legacy, 12, static_cast<uint32_t>(legacy.size() - 20));
    refreshSaveChecksum(legacy);
    writeBytes(save.path, legacy);
    require(game.load(save.path.string()), "valid version 2 save was rejected");
    require(game.completedMissions == 4 && game.money == 7654 &&
            game.missionInfo() == &mc::Game::missions()[4],
            "version 2 migration lost progress or failed to unlock the rescue contract");
    require(game.vehicles.size() == vehicleCount && close(game.vehicles[0].pitch, 0.14f) &&
            close(game.vehicles[0].roll, -0.2f) && close(game.vehicles[0].throttle, 0.3f),
            "version 2 migration changed the fleet or craft orientation");
    require(game.save(save.path.string()), "migrated version 2 save could not be upgraded");
    std::ifstream upgradedInput(save.path, std::ios::binary);
    const std::vector<char> upgraded((std::istreambuf_iterator<char>(upgradedInput)), {});
    require(littleEndian(upgraded, 8) == 4 && littleEndian(upgraded, upgraded.size() - 16) == 0,
            "version 2 migration did not initialize the rescue hold to zero");
}

size_t vehicleOfKind(const mc::Game& game, mc::VehicleKind kind) {
    for (size_t i = 0; i < game.vehicles.size(); ++i)
        if (game.vehicles[i].kind == kind) return i;
    throw std::runtime_error("contract did not supply its required craft");
}

void acceptCraftContract(mc::Game& game, int chapter) {
    tick(game);
    mc::Input accept;
    accept.mission = true;
    tick(game, accept);
    require(game.activeMission == chapter, "contact did not start the craft contract");
    tick(game);
}

void positionCraft(mc::Game& game, size_t index, mc::Vec3 position, float speed = 0) {
    mc::Vehicle& craft = game.vehicles[index];
    craft.position = position;
    craft.yaw = craft.pitch = craft.roll = 0;
    craft.speed = speed;
    craft.velocity = mc::forward(craft.yaw) * speed;
    craft.throttle = speed > 0 ? 0.7f : 0;
    game.occupied = static_cast<int>(index);
    game.player = position;
}

void rescueContractAndHoldPersistence() {
    mc::Game game;
    game.completedMissions = 4;
    mc::Vehicle car;
    car.position = mc::Game::missions()[4].start;
    car.parked = true;
    game.vehicles.push_back(car);
    game.player = car.position;
    game.occupied = 0;
    const int initialMoney = game.money;
    acceptCraftContract(game, 4);
    require(game.missionStage == 0, "rescue accepted a road vehicle as its boat");
    const size_t boat = vehicleOfKind(game, mc::VehicleKind::Boat);
    positionCraft(game, boat, game.vehicles[boat].position);
    tick(game);
    require(game.missionStage == 1, "boarding the rescue boat did not advance the contract");
    const mc::Vec3 alongside{3080, mc::World::WaterLevel, 1080};
    positionCraft(game, boat, alongside, 3);
    tick(game, {}, 185);
    require(game.missionStage == 1, "rescue transfer completed while the boat was moving too fast");
    positionCraft(game, boat, alongside);
    tick(game, {}, 90);
    require(game.missionStage == 1, "rescue transfer completed before three seconds");
    positionCraft(game, boat, {3050, mc::World::WaterLevel, 1080});
    tick(game);
    positionCraft(game, boat, alongside);
    tick(game, {}, 95);
    require(game.missionStage == 1, "leaving the rescue radius did not reset the transfer hold");
    TemporarySave save;
    require(game.save(save.path.string()), "rescue hold could not be saved");
    std::ifstream input(save.path, std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(input)), {});
    input.close();
    for (uint32_t invalidHold : {0x7fc00000u, 0xbf800000u, 0x40800000u}) {
        std::vector<char> corrupted = original;
        setLittleEndian(corrupted, corrupted.size() - 16, invalidHold);
        refreshSaveChecksum(corrupted);
        writeBytes(save.path, corrupted);
        require(!game.load(save.path.string()) && game.activeMission == 4 && game.missionStage == 1,
                "invalid rescue hold was accepted or partially overwrote mission state");
    }
    writeBytes(save.path, original);
    require(game.load(save.path.string()), "rescue hold could not be restored");
    tick(game, {}, 90);
    require(game.missionStage == 2, "saved rescue transfer hold did not resume after loading");
    positionCraft(game, boat, {2678, mc::World::WaterLevel, 768});
    tick(game);
    require(game.completedMissions == 5 && game.activeMission == -1 &&
            game.money == initialMoney + mc::Game::missions()[4].reward,
            "returning the rescued passenger did not complete and reward the contract");
    tick(game, {}, 3);
    require(game.money == initialMoney + 1800, "rescue reward was duplicated or incorrect");
}

void surveyContractFlightAndLanding() {
    mc::Game game;
    game.completedMissions = 5;
    game.player = mc::Game::missions()[5].start;
    const int initialMoney = game.money;
    acceptCraftContract(game, 5);
    const size_t plane = vehicleOfKind(game, mc::VehicleKind::Aircraft);
    mc::Vehicle car;
    car.position = game.missionTarget();
    car.position.y = game.world.height(car.position.x, car.position.z);
    car.parked = true;
    game.vehicles.push_back(car);
    positionCraft(game, game.vehicles.size() - 1, car.position, 30);
    tick(game);
    require(game.missionStage == 0, "survey accepted a road vehicle at a flight gate");
    const mc::Vec3 gate = game.missionTarget();
    const float ground = game.world.height(gate.x, gate.z);
    positionCraft(game, plane, gate + mc::Vec3{100, 0, 0}, 40);
    tick(game);
    require(game.missionStage == 0, "survey accepted a pass outside the gate radius");
    for (float altitude : {20.0f, 180.0f}) {
        positionCraft(game, plane, {gate.x, ground + altitude, gate.z}, 40);
        tick(game);
        require(game.missionStage == 0, "survey accepted a flight outside its altitude band");
    }
    positionCraft(game, plane, gate);
    tick(game);
    require(game.missionStage == 0, "survey accepted a gate without flight speed");
    for (int stage = 0; stage < 3; ++stage) {
        positionCraft(game, plane, game.missionTarget(), 40);
        tick(game);
        require(game.missionStage == stage + 1, "valid survey pass did not advance exactly one gate");
    }
    mc::Vec3 runway = game.missionTarget();
    runway.y = game.world.height(runway.x, runway.z);
    positionCraft(game, game.vehicles.size() - 1, runway);
    tick(game);
    require(game.activeMission == 5, "survey accepted a car instead of landing its aircraft");
    positionCraft(game, plane, runway + mc::Vec3{0, 2, 0});
    tick(game);
    require(game.activeMission == 5, "survey completed while the aircraft was still airborne");
    positionCraft(game, plane, runway, 10);
    tick(game);
    require(game.activeMission == 5, "survey completed before the aircraft slowed down");
    positionCraft(game, plane, runway + mc::Vec3{40, 0, 0});
    tick(game);
    require(game.activeMission == 5, "survey accepted a stop outside the runway corridor");
    positionCraft(game, plane, runway);
    tick(game);
    require(game.completedMissions == 6 && game.activeMission == -1 && game.missionInfo() == nullptr,
            "survey landing did not finish the six-contract campaign");
    require(game.money == initialMoney + 2600, "survey reward was incorrect");
    TemporarySave save;
    require(game.save(save.path.string()) && game.load(save.path.string()) &&
            game.completedMissions == 6 && game.missionInfo() == nullptr,
            "completed six-contract campaign did not survive save/load");
}

void craftContractFailureAndRecovery() {
    for (int chapter : {4, 5}) {
        mc::Game game;
        game.completedMissions = chapter;
        game.player = mc::Game::missions()[chapter].start;
        mc::Vehicle broken;
        broken.kind = chapter == 4 ? mc::VehicleKind::Boat : mc::VehicleKind::Aircraft;
        broken.position = {0, 0, 0};
        broken.health = 0;
        game.vehicles.push_back(broken);
        const int initialMoney = game.money;
        acceptCraftContract(game, chapter);
        require(game.vehicles.size() == 1 && close(game.vehicles[0].health, 100),
                "contact did not recover an existing destroyed loan craft");
        require(mc::length(game.vehicles[0].position - game.player) < 50,
                "replacement loan craft was not placed near its contact");
        game.missionTimer = 0.001f;
        tick(game);
        require(game.activeMission == -1 && game.completedMissions == chapter && game.money == initialMoney,
                "expired craft contract advanced progress or paid a reward");
        acceptCraftContract(game, chapter);
        require(game.missionTimer > 200, "craft contract retry did not reset its deadline");
        game.missionStage = chapter == 4 ? 2 : 3;
        const mc::Vec3 finish = chapter == 4 ? mc::Vec3{2678, mc::World::WaterLevel, 768}
            : mc::Vec3{-3200, game.world.height(-3200, -1000), -1000};
        positionCraft(game, 0, finish);
        game.vehicles[0].health = 0;
        tick(game);
        require(game.activeMission == -1 && game.completedMissions == chapter && game.money == initialMoney,
                "destroyed craft completed its contract in the delivery zone");
        game.occupied = -1;
        game.player = mc::Game::missions()[chapter].start;
        game.vehicles[0].health = 100;
        game.vehicles[0].position = {400, 0, 400};
        acceptCraftContract(game, chapter);
        require(game.vehicles.size() == 1 && game.vehicles[0].health > 0 &&
                mc::length(game.vehicles[0].position - game.player) < 50,
                "contact did not recover a stranded unoccupied loan craft");
    }

    mc::Game fatalLanding;
    fatalLanding.completedMissions = fatalLanding.activeMission = 5;
    fatalLanding.missionStage = 3;
    fatalLanding.missionTimer = 100;
    fatalLanding.health = 1;
    fatalLanding.money = 1234;
    mc::Vehicle aircraft;
    aircraft.kind = mc::VehicleKind::Aircraft;
    aircraft.position = {-3200, fatalLanding.world.height(-3200, -1000) + 0.05f, -1000};
    aircraft.velocity.y = -5;
    fatalLanding.vehicles.push_back(aircraft);
    fatalLanding.player = aircraft.position;
    fatalLanding.occupied = 0;
    fatalLanding.update({}, 0.05f);
    require(fatalLanding.vehicles[0].health > 0 && fatalLanding.completedMissions == 5 &&
            fatalLanding.money <= 1234,
            "fatal landing paid the survey reward and revived the pilot");
}

void craftLoansPreserveOccupiedVehicles() {
    for (int chapter : {4, 5}) {
        mc::Game game;
        game.completedMissions = chapter;
        mc::Vehicle current;
        current.kind = chapter == 4 ? mc::VehicleKind::Boat : mc::VehicleKind::Aircraft;
        current.position = chapter == 4 ? mc::Vec3{2678, mc::World::WaterLevel, 768}
            : mc::Vec3{-3195, game.world.height(-3195, -1185), -1185};
        current.health = 73;
        game.vehicles.push_back(current);
        game.player = current.position;
        game.occupied = 0;
        acceptCraftContract(game, chapter);
        require(game.occupied == 0 && game.vehicles.size() == 2 && close(game.vehicles[0].health, 73),
                "provisioning a loan replaced or repaired the occupied craft");
        require(close(game.vehicles[0].position.x, current.position.x) &&
                close(game.vehicles[0].position.z, current.position.z),
                "provisioning a loan teleported the occupied craft");
        require(game.vehicles[1].kind == current.kind && close(game.vehicles[1].health, 100) &&
                mc::length(game.vehicles[1].position - game.vehicles[0].position) > 10,
                "replacement loan craft overlapped the occupied craft");
    }
}

void craftContractVisualsSurviveLoad() {
    mc::Game game;
    game.completedMissions = game.activeMission = 4;
    game.missionStage = 1;
    game.missionTimer = 200;
    mc::Vehicle boat;
    boat.kind = mc::VehicleKind::Boat;
    boat.position = {3080, mc::World::WaterLevel, 1080};
    game.vehicles.push_back(boat);
    game.player = boat.position;
    game.occupied = 0;
    const auto nearbyVertices = [](const mc::Mesh& mesh, mc::Vec3 center,
                                  float halfX, float halfZ, float low, float high) {
        return std::count_if(mesh.vertices.begin(), mesh.vertices.end(),
            [&](const mc::Vertex& vertex) {
                const mc::Vec3 relative = vertex.position - center;
                return std::fabs(relative.x) < halfX && std::fabs(relative.z) < halfZ &&
                    relative.y > low && relative.y < high;
            });
    };
    const mc::Vec3 clinic{3090, mc::World::WaterLevel, 1080};
    TemporarySave save;
    require(game.save(save.path.string()) && game.load(save.path.string()),
            "pickup visual fixture could not be saved and restored");
    const mc::Mesh pickup = game.dynamicMesh();
    verifyDynamicMesh(pickup, "restored rescue pickup");
    require(nearbyVertices(pickup, clinic, 1.5f, 2.5f, 0.3f, 2.9f) > 100,
            "restored rescue pickup has no survivor and supplies at the clinic launch");

    struct CarrierFixture {
        mc::VehicleKind kind;
        const char* name;
        mc::Vec3 seat, supplies;
    };
    const CarrierFixture carriers[] = {
        {mc::VehicleKind::Boat, "boat", {-.54f, .04f, -1.31f}, {.56f, .64f, .67f}},
        {mc::VehicleKind::Car, "car", {.40f, -.31f, -.02f}, {0, .94f, -1.55f}},
        {mc::VehicleKind::Motorcycle, "motorcycle", {0, .08f, -.50f}, {-.42f, .45f, -.63f}},
        {mc::VehicleKind::Aircraft, "aircraft", {.15f, .23f, .55f}, {-.10f, 1.43f, 0}},
    };
    const auto componentVertices = [](const mc::Mesh& mesh, mc::Vec3 color) {
        std::vector<mc::Vertex> component;
        std::vector<bool> drawn(mesh.vertices.size(), false);
        for (uint32_t index : mesh.indices) {
            require(index < drawn.size(), "passenger mesh has an invalid triangle index");
            drawn[index] = true;
        }
        for (size_t i = 0; i < mesh.vertices.size(); ++i)
            if (drawn[i] && mc::length(mesh.vertices[i].color - color) < .00001f)
                component.push_back(mesh.vertices[i]);
        return component;
    };
    const mc::Vec3 patientCoat{.79f, .27f, .065f}, medicalCross{.76f, .075f, .045f};
    for (const CarrierFixture& fixture : carriers) {
        game = mc::Game{};
        game.completedMissions = game.activeMission = 4;
        game.missionStage = 2;
        game.missionTimer = 200;
        mc::Vehicle carrier;
        carrier.kind = fixture.kind;
        carrier.position = {3080, fixture.kind == mc::VehicleKind::Aircraft ? 80.0f :
                            mc::World::WaterLevel, 1080};
        game.vehicles.push_back(carrier);
        game.player = carrier.position;
        game.occupied = 0;
        const std::string label = std::string("rescued passenger in ") + fixture.name;
        const mc::Mesh reference = game.dynamicMesh();
        const auto coat = componentVertices(reference, patientCoat);
        const auto cross = componentVertices(reference, medicalCross);
        require(coat.size() > 50, (label + " has no visible patient coat").c_str());
        require(cross.size() >= 24, (label + " has no medical case cross").c_str());
        for (const mc::Vertex& vertex : coat) {
            const mc::Vec3 offset = vertex.position - carrier.position - fixture.seat;
            require(std::fabs(offset.x) < .5f && offset.y > .5f && offset.y < 1.5f &&
                    std::fabs(offset.z) < .75f,
                    (label + " is outside the passenger seat").c_str());
        }
        for (const mc::Vertex& vertex : cross)
            require(mc::length(vertex.position - carrier.position - fixture.supplies -
                               mc::Vec3{0, .2f, 0}) < .45f,
                    (label + " has medical supplies outside their carrier mount").c_str());
        if (fixture.kind == mc::VehicleKind::Car || fixture.kind == mc::VehicleKind::Aircraft) {
            const mc::Box cabin = fixture.kind == mc::VehicleKind::Car ?
                mc::Box{{-.83f, .38f, -1}, {.83f, 1.5f, 1}} :
                mc::Box{{-.39f, 1.32f, -.25f}, {.39f, 2.1f, 1.42f}};
            for (mc::Vec3 actorColor : {patientCoat, mc::Vec3{.59f, .36f, .23f},
                                       mc::Vec3{.055f, .065f, .081f}, mc::Vec3{.029f, .024f, .022f}})
                for (const mc::Vertex& vertex : componentVertices(reference, actorColor)) {
                    const mc::Vec3 point = vertex.position - carrier.position;
                    require(point.x >= cabin.min.x - .003f && point.x <= cabin.max.x + .003f &&
                            point.y >= cabin.min.y - .003f && point.y <= cabin.max.y + .003f &&
                            point.z >= cabin.min.z - .003f && point.z <= cabin.max.z + .003f,
                            (label + " protrudes through the cabin floor or walls").c_str());
                }
        }

        game.vehicles[0].position += mc::Vec3{7, 13, -9};
        game.vehicles[0].yaw = mc::Pi * .5f;
        const bool flying = fixture.kind == mc::VehicleKind::Aircraft;
        if (flying) {
            game.vehicles[0].pitch = mc::Pi / 6;
            game.vehicles[0].roll = -mc::Pi / 6;
        }
        game.player = game.vehicles[0].position;
        const mc::Vec3 restoredOrigin = game.player;
        require(game.save(save.path.string()) && game.load(save.path.string()),
                (label + " could not be saved and restored").c_str());
        const mc::Mesh restored = game.dynamicMesh();
        verifyDynamicMesh(restored, label.c_str());
        // Known basis for yaw 90 degrees, pitch 30 degrees, and roll -30 degrees.
        // Road vehicles and the level boat need only the quarter-turn yaw.
        const auto oriented = [flying](mc::Vec3 local) {
            if (!flying) return mc::Vec3{local.z, local.y, -local.x};
            return mc::Vec3{-.25f, .4330127f, -.8660254f} * local.x +
                   mc::Vec3{-.4330127f, .75f, .5f} * local.y +
                   mc::Vec3{.8660254f, .5f, 0} * local.z;
        };
        for (mc::Vec3 color : {patientCoat, medicalCross}) {
            const auto before = componentVertices(reference, color);
            const auto after = componentVertices(restored, color);
            require(after.size() == before.size(),
                    (label + " lost patient or supply geometry after load").c_str());
            for (size_t i = 0; i < before.size(); ++i) {
                const mc::Vec3 expected = restoredOrigin +
                    oriented(before[i].position - carrier.position);
                require(mc::length(after[i].position - expected) < .003f,
                        (label + " does not follow the carrier position and attitude").c_str());
                require(mc::length(after[i].normal - oriented(before[i].normal)) < .002f,
                        (label + " normals do not follow the carrier attitude").c_str());
            }
        }
    }

    game = mc::Game{};
    game.completedMissions = game.activeMission = 5;
    game.missionTimer = 300;
    const mc::Vec3 gate = game.missionTarget();
    game.player = gate + mc::Vec3{0, 0, -100};
    const mc::Mesh survey = game.dynamicMesh();
    verifyDynamicMesh(survey, "airborne survey gate");
    require(nearbyVertices(survey, gate, 80, 3, 38, 42) > 10 &&
            nearbyVertices(survey, gate, 80, 3, -42, -38) > 10,
            "survey gate was flattened to the terrain instead of retaining its flight altitude");
}

void requestHarborSplit(mc::Game& game) {
    game.occupied = -1;
    game.player = mc::Game::harborSplitContact();
    game.wanted = 0;
    tick(game);
    mc::Input request;
    request.mission = true;
    tick(game, request);
    require(game.harborSplit.phase == mc::TrialPhase::Boarding,
            "Harbor Split did not offer a motorcycle at its contact");
}

size_t beginHarborSplit(mc::Game& game) {
    requestHarborSplit(game);
    // A replacement bay can sit farther along the curb when the old vehicle blocks it.
    for (int frame = 0; frame < 300 && mc::length(game.objectiveTarget() - game.player) > 5.8f; ++frame) {
        const mc::Vec3 direction = mc::normalized(game.objectiveTarget() - game.player);
        mc::Input approach;
        approach.moveY = mc::dot(direction, mc::forward(game.yaw));
        approach.moveX = mc::dot(direction, mc::right(game.yaw));
        tick(game, approach);
    }
    mc::Input board;
    board.interact = true;
    tick(game, board);
    require(game.occupied >= 0 && game.harborSplit.phase == mc::TrialPhase::Countdown,
            "boarding the loan bike did not begin the start countdown");
    const size_t bike = size_t(game.occupied);
    require(game.vehicles[bike].kind == mc::VehicleKind::Motorcycle,
            "Harbor Split supplied the wrong vehicle kind");
    tick(game, {}, 181);
    require(game.harborSplit.phase == mc::TrialPhase::Running,
            "Harbor Split countdown did not release the rider");
    return bike;
}

void finishHarborSplit(mc::Game& game, size_t bike, float time) {
    const auto& course = mc::Game::harborSplitCourse();
    for (size_t i = size_t(game.harborSplit.checkpoint); i < course.size(); ++i) {
        positionCraft(game, bike, course[i]);
        if (i + 1 == course.size()) game.harborSplit.elapsed = time - 1.0f / 60.0f;
        tick(game);
        if (i + 1 < course.size())
            require(game.harborSplit.checkpoint == int(i + 1),
                    "an ordered motorcycle gate failed to advance exactly once");
    }
    require(game.harborSplit.phase == mc::TrialPhase::Inactive,
            "the final Harbor Split gate did not end the run");
}

void harborSplitStartAndObjectives() {
    mc::Game game;
    require(!game.objectiveIsTrial() && game.objectiveInfo() == game.missionInfo(),
            "distant side activity displaced the early-game story objective");
    mc::Input request;
    request.mission = true;
    game.player = mc::Game::harborSplitContact();
    require(game.objectiveIsTrial(), "nearby trial contact was not identified as a trial objective");
    game.activeMission = 0;
    tick(game, request);
    require(game.harborSplit.phase == mc::TrialPhase::Inactive && game.activeMission == 0,
            "Harbor Split replaced an active story mission");
    require(!game.objectiveIsTrial() && game.objectiveInfo() == game.missionInfo() &&
            mc::length(game.objectiveTarget() - game.missionTarget()) < .001f,
            "inactive side activity changed the story objective");
    game.activeMission = -1;
    game.wanted = 3;
    tick(game);
    tick(game, request);
    require(game.wanted > 0 && game.harborSplit.phase == mc::TrialPhase::Inactive,
            "Harbor Split accepted a rider under pursuit");

    requestHarborSplit(game);
    require(game.activeMission == -1 && game.completedMissions == 0 &&
            game.missionInfo() == &mc::Game::missions()[0],
            "side activity changed campaign progress or mission API semantics");
    require(game.objectiveIsTrial() && game.objectiveInfo() != game.missionInfo() && game.objectiveActive() &&
            std::string(game.objectiveInfo()->title) == "HARBOR SPLIT",
            "side activity was not presented as the current objective");
    verifyDynamicMesh(game.dynamicMesh(), "Harbor Split staging");
    tick(game, request, 5);
    require(game.harborSplit.phase == mc::TrialPhase::Boarding,
            "holding M repeatedly cancelled or restarted the side activity");
    mc::Input board;
    board.interact = true;
    tick(game, board);
    require(game.harborSplit.phase == mc::TrialPhase::Countdown,
            "the accessible curbside loan bike did not reach countdown");
    verifyDynamicMesh(game.dynamicMesh(), "Harbor Split countdown");
    const mc::Vec3 start = game.player;
    mc::Input accelerate;
    accelerate.moveY = accelerate.moveX = 1;
    tick(game, accelerate, 90);
    require(mc::length(game.player - start) < .001f &&
            close(game.vehicles[size_t(game.occupied)].speed, 0) &&
            close(game.harborSplit.elapsed, 0),
            "countdown allowed acceleration, steering, or a false start");
    const float countdown = game.harborSplit.countdown;
    game.paused = true;
    tick(game, accelerate, 180);
    require(close(game.harborSplit.countdown, countdown), "pause advanced the race countdown");
    game.paused = false;
    tick(game, {}, 92);
    require(game.harborSplit.phase == mc::TrialPhase::Running &&
            game.harborSplit.checkpoint == 0 && game.harborSplit.elapsed < .1f,
            "countdown did not transition cleanly into the first gate");
    require(mc::length(game.objectiveTarget() - mc::Game::harborSplitCourse()[0]) < .001f &&
            game.objectiveTimeRemaining() > mc::Game::harborSplitLimit() - .1f &&
            std::string(game.objectiveInstruction()).find("GATE 1") != std::string::npos,
            "running objective omitted its gate or remaining time");
    verifyDynamicMesh(game.dynamicMesh(), "Harbor Split first gate");
}

void harborSplitFailuresAndLoans() {
    for (int failure = 0; failure < 7; ++failure) {
        mc::Game game;
        game.harborSplit.bestTime = 80;
        game.harborSplit.medal = 3;
        const size_t bike = beginHarborSplit(game);
        const int money = game.money;
        mc::Input action;
        if (failure == 0) action.mission = true;
        if (failure == 1) action.interact = true;
        if (failure == 2) game.vehicles[bike].health = 0;
        if (failure == 3) game.harborSplit.elapsed = mc::Game::harborSplitLimit();
        if (failure == 4) game.vehicles[bike].kind = mc::VehicleKind::Car;
        if (failure == 5) {
            mc::Vehicle replacement = game.vehicles[bike];
            replacement.position = mc::Game::harborSplitCourse()[0];
            game.vehicles.push_back(replacement);
            game.occupied = int(game.vehicles.size() - 1);
            game.player = replacement.position;
        }
        if (failure == 6) game.health = 0;
        tick(game, action);
        const int expectedMoney = failure == 6 ? std::max(0, money - 100) : money;
        require(game.harborSplit.phase == mc::TrialPhase::Inactive && game.money == expectedMoney &&
                close(game.harborSplit.bestTime, 80) && game.harborSplit.medal == 3,
                "withdrawal, dismount, vehicle loss, swap, or timeout awarded/reset a record");
        require(game.activeMission == -1 && game.completedMissions == 0,
                "failed side activity changed campaign progress");
        const size_t replacement = beginHarborSplit(game);
        require(game.vehicles[replacement].health > 0 &&
                game.vehicles[replacement].kind == mc::VehicleKind::Motorcycle,
                "a failed side activity could not provide a usable retry bike");
    }
    mc::Game expired;
    requestHarborSplit(expired);
    expired.harborSplit.countdown = .001f;
    tick(expired);
    require(expired.harborSplit.phase == mc::TrialPhase::Inactive,
            "unattended boarding offer never expired");

    mc::Game ownBike;
    mc::Vehicle bike;
    bike.kind = mc::VehicleKind::Motorcycle;
    bike.position = mc::Game::harborSplitStart();
    bike.health = 67;
    ownBike.vehicles.push_back(bike);
    ownBike.player = bike.position;
    ownBike.occupied = 0;
    mc::Input request;
    request.mission = true;
    tick(ownBike, request);
    require(ownBike.harborSplit.phase == mc::TrialPhase::Countdown &&
            ownBike.vehicles.size() == 1 && close(ownBike.vehicles[0].health, 67) &&
            mc::length(ownBike.player - bike.position) < .001f,
            "entering on an owned motorcycle repaired, replaced, or teleported it");
}

void harborSplitGatesAndRewards() {
    mc::Game game;
    size_t bike = beginHarborSplit(game);
    const auto& course = mc::Game::harborSplitCourse();
    require(course.size() >= 6 && course.size() <= 10, "time trial has an invalid gate count");
    positionCraft(game, bike, course.back());
    tick(game);
    require(game.harborSplit.checkpoint == 0 && game.harborSplit.medal == 0,
            "touching the finish early skipped the ordered course");
    positionCraft(game, bike, course[0] + mc::Vec3{0, 0, -8});
    tick(game);
    require(game.harborSplit.checkpoint == 0, "gate accepted a rider outside its radius");
    positionCraft(game, bike, course[0]);
    tick(game, {}, 3);
    require(game.harborSplit.checkpoint == 1, "lingering at a gate advanced several checkpoints");
    const int initialMoney = game.money;
    finishHarborSplit(game, bike, 120);
    require(game.harborSplit.medal == 1 && close(game.harborSplit.bestTime, 120) &&
            game.money == initialMoney + 150, "bronze finish did not save and pay its record");
    bike = beginHarborSplit(game);
    finishHarborSplit(game, bike, 110);
    require(game.harborSplit.medal == 2 && close(game.harborSplit.bestTime, 110) &&
            game.money == initialMoney + 350, "silver improvement did not pay only the medal difference");
    bike = beginHarborSplit(game);
    finishHarborSplit(game, bike, 85);
    require(game.harborSplit.medal == 3 && close(game.harborSplit.bestTime, 85) &&
            game.money == initialMoney + 650, "gold improvement did not pay only the medal difference");
    bike = beginHarborSplit(game);
    finishHarborSplit(game, bike, 84);
    require(close(game.harborSplit.bestTime, 84) && game.money == initialMoney + 650,
            "improving within the same medal tier duplicated its prize or lost the faster time");
    bike = beginHarborSplit(game);
    finishHarborSplit(game, bike, 90);
    require(game.harborSplit.medal == 3 && close(game.harborSplit.bestTime, 84) &&
            game.money == initialMoney + 650, "a slower repeat duplicated the prize or lost the best record");
    mc::Game penalized;
    bike = beginHarborSplit(penalized);
    penalized.harborSplit.penalty = 5;
    finishHarborSplit(penalized, bike, 82);
    require(penalized.harborSplit.medal == 2 && close(penalized.harborSplit.bestTime, 87),
            "penalty time was excluded from the medal and saved record");
}

void harborSplitDamagePenalties() {
    mc::Game game;
    const size_t bike = beginHarborSplit(game);
    const mc::Vec3 impactSite = mc::Game::harborSplitStart();
    const auto collide = [&]() {
        positionCraft(game, bike, impactSite, 20);
        game.world.chunks.front().solids.push_back(
            {impactSite + mc::Vec3{-3, 0, .6f}, impactSite + mc::Vec3{3, 3, .85f}});
        const float health = game.vehicles[bike].health;
        game.update({}, .05f);
        game.world.chunks.front().solids.pop_back();
        require(game.vehicles[bike].health < health, "penalty fixture did not cause a real wall impact");
    };
    collide();
    require(close(game.harborSplit.penalty, 5), "vehicle collision did not add five penalty seconds");
    collide();
    require(close(game.harborSplit.penalty, 5), "one impact sequence generated repeated penalties");
    positionCraft(game, bike, impactSite);
    tick(game, {}, 65);
    collide();
    require(close(game.harborSplit.penalty, 10), "a later impact did not add a new penalty");
    game.harborSplit.elapsed = mc::Game::harborSplitLimit() - 9;
    tick(game);
    require(game.harborSplit.phase == mc::TrialPhase::Inactive && game.harborSplit.medal == 0,
            "penalty time did not count toward the overall race timeout");
}

void harborSplitSaveAndCorruption() {
    mc::Game game;
    const size_t bike = beginHarborSplit(game);
    game.harborSplit.bestTime = 80;
    game.harborSplit.medal = 3;
    game.harborSplit.elapsed = 30;
    game.harborSplit.penalty = 5;
    positionCraft(game, bike, mc::Game::harborSplitCourse()[0]);
    tick(game);
    const mc::Vec3 savedPosition = game.player;
    const int savedMoney = game.money;
    TemporarySave save;
    require(game.save(save.path.string()), "active Harbor Split run could not be saved");
    std::ifstream input(save.path, std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(input)), {});
    input.close();
    require(littleEndian(original, 8) == 4, "Harbor Split records did not use save version 4");
    require(game.load(save.path.string()), "Harbor Split records could not be restored");
    require(game.harborSplit.phase == mc::TrialPhase::Inactive &&
            game.harborSplit.checkpoint == 0 && close(game.harborSplit.elapsed, 0) &&
            close(game.harborSplit.penalty, 0) && game.harborSplit.medal == 3 &&
            close(game.harborSplit.bestTime, 80) && game.money == savedMoney &&
            mc::length(game.player - savedPosition) < .001f &&
            game.message.find("interrupted") != std::string::npos,
            "loading resumed a timed attempt, lost its record, moved the rider, or awarded money");
    const auto reject = [&](std::vector<char> bytes) {
        refreshSaveChecksum(bytes);
        writeBytes(save.path, bytes);
        require(!game.load(save.path.string()), "invalid time-trial save data was accepted");
        require(close(game.harborSplit.bestTime, 80) && game.harborSplit.medal == 3 &&
                game.money == savedMoney && mc::length(game.player - savedPosition) < .001f,
                "invalid time-trial save partially overwrote the current game");
    };
    for (uint32_t invalid : {0x7fc00000u, 0x7f800000u, 0xbf800000u, 0x43160000u, 0u}) {
        auto bytes = original;
        setLittleEndian(bytes, bytes.size() - 12, invalid);
        reject(bytes);
    }
    for (uint32_t invalid : {0u, 1u, 2u, 4u, 0xffffffffu}) {
        auto bytes = original;
        setLittleEndian(bytes, bytes.size() - 8, invalid);
        reject(bytes);
    }
    auto bytes = original;
    setLittleEndian(bytes, bytes.size() - 4, 2);
    reject(bytes);
    bytes = original;
    bytes.resize(bytes.size() - 1);
    setLittleEndian(bytes, 12, uint32_t(bytes.size() - 20));
    reject(bytes);
    writeBytes(save.path, original);
    require(game.load(save.path.string()), "valid records could not load after rejected corruption");
    beginHarborSplit(game);
    require(game.harborSplit.medal == 3 && close(game.harborSplit.bestTime, 80),
            "retry after loading discarded the saved best record");
}

void versionThreeSaveMigration() {
    mc::Game game;
    game.completedMissions = game.activeMission = 4;
    game.missionStage = 1;
    game.missionTimer = 180;
    mc::Vehicle boat;
    boat.kind = mc::VehicleKind::Boat;
    boat.position = {3080, mc::World::WaterLevel, 1080};
    game.vehicles.push_back(boat);
    positionCraft(game, 0, boat.position);
    tick(game, {}, 95);
    game.harborSplit.bestTime = 80;
    game.harborSplit.medal = 3;
    TemporarySave save;
    require(game.save(save.path.string()), "could not create version 3 migration fixture");
    std::ifstream input(save.path, std::ios::binary);
    std::vector<char> legacy((std::istreambuf_iterator<char>(input)), {});
    input.close();
    require(littleEndian(legacy, 8) == 4, "version 3 fixture requires a version 4 source save");
    legacy.resize(legacy.size() - 12);
    setLittleEndian(legacy, 8, 3);
    setLittleEndian(legacy, 12, uint32_t(legacy.size() - 20));
    refreshSaveChecksum(legacy);
    writeBytes(save.path, legacy);
    require(game.load(save.path.string()), "valid version 3 save was rejected");
    require(game.harborSplit.phase == mc::TrialPhase::Inactive &&
            game.harborSplit.medal == 0 && close(game.harborSplit.bestTime, 0) &&
            game.activeMission == 4 && game.missionStage == 1,
            "version 3 migration did not preserve the campaign and initialize empty trial records");
    tick(game, {}, 90);
    require(game.missionStage == 2, "version 3 migration lost the in-progress rescue transfer hold");
}

void harborSplitDrivenCourse() {
    std::vector<mc::Vec3> corners{mc::Game::harborSplitStart()};
    const auto& gates = mc::Game::harborSplitCourse();
    corners.insert(corners.end(), gates.begin(), gates.end());
    std::vector<mc::Vec3> path{corners.front()};
    const auto lineTo = [&](mc::Vec3 end) {
        const mc::Vec3 start = path.back();
        const int steps = std::max(1, int(std::ceil(mc::length(end - start))));
        for (int i = 1; i <= steps; ++i) path.push_back(mc::lerp(start, end, float(i) / steps));
    };
    // Rounded approaches stay inside each intersection; the controller must steer them.
    for (size_t i = 1; i + 1 < corners.size(); ++i) {
        const mc::Vec3 incoming = mc::normalized(corners[i] - corners[i - 1]);
        const mc::Vec3 outgoing = mc::normalized(corners[i + 1] - corners[i]);
        const mc::Vec3 entry = corners[i] - incoming * 8;
        const mc::Vec3 exit = corners[i] + outgoing * 8;
        lineTo(entry);
        for (int step = 1; step <= 24; ++step) {
            const float t = float(step) / 24;
            path.push_back(entry * ((1 - t) * (1 - t)) + corners[i] * (2 * t * (1 - t)) +
                           exit * (t * t));
        }
    }
    lineTo(corners.back());
    std::vector<float> arc(path.size());
    for (size_t i = 1; i < path.size(); ++i) arc[i] = arc[i - 1] + mc::length(path[i] - path[i - 1]);
    require(arc.back() > 1200 && arc.back() < 1800, "time trial is outside its intended road distance");

    mc::Game game;
    game.initialize();
    const size_t population = game.pedestrians.size(), fleet = game.vehicles.size();
    const size_t bikeIndex = beginHarborSplit(game);
    const int startingMoney = game.money;
    size_t progress = 0;
    float maxDeviation = 0, peakPenalty = 0, steeringTravel = 0, passingOffset = 0;
    for (int frame = 0; frame < 60 * 155 && game.harborSplit.phase == mc::TrialPhase::Running; ++frame) {
        const mc::Vehicle& bike = game.vehicles[bikeIndex];
        size_t closest = progress;
        float deviation = mc::length(bike.position - path[progress]);
        for (size_t i = progress; i < std::min(path.size(), progress + 80); ++i) {
            const float distance = mc::length(bike.position - path[i]);
            if (distance < deviation) { closest = i; deviation = distance; }
        }
        progress = closest;
        maxDeviation = std::max(maxDeviation, deviation);
        const float lookahead = mc::clamp(3 + std::abs(bike.speed) * .27f, 4, 11);
        size_t aim = progress;
        while (aim + 1 < path.size() && arc[aim] < arc[progress] + lookahead) ++aim;
        float desiredOffset = 0;
        for (size_t i = 0; i < game.vehicles.size(); ++i) {
            if (i == bikeIndex || std::abs(game.vehicles[i].speed) > 2) continue;
            for (size_t j = progress; j < path.size() && arc[j] < arc[progress] + 30; ++j)
                if (mc::length(game.vehicles[i].position - path[j]) < 2.3f) {
                    desiredOffset = 3.2f;
                    break;
                }
        }
        // Use the clear road shoulder to pass stationary traffic without cutting the corner gate.
        passingOffset = mc::lerp(passingOffset, desiredOffset, 1.0f / 12.0f);
        const mc::Vec3 tangent = mc::normalized(path[std::min(path.size() - 1, aim + 2)] -
                                               path[aim > 2 ? aim - 2 : 0]);
        const mc::Vec3 goal = path[aim] + mc::Vec3{tangent.z, 0, -tangent.x} * passingOffset;
        const mc::Vec3 delta = goal - bike.position;
        const float error = mc::wrapAngle(std::atan2(delta.x, delta.z) - bike.yaw);
        mc::Input input;
        input.moveX = mc::clamp(2 * 1.85f * (1 + .03f * std::abs(bike.speed)) * std::sin(error) /
                               (.61f * std::max(1.0f, mc::length(delta))), -1, 1);
        float targetSpeed = std::abs(passingOffset) > .3f ? 16.0f : 28.0f;
        const float brakingDistance = std::max(16.0f, (bike.speed * bike.speed - 144) / 35 + 8);
        for (size_t i = progress + 2; i + 2 < path.size() && arc[i] < arc[progress] + brakingDistance; ++i) {
            const mc::Vec3 a = mc::normalized(path[i] - path[i - 2]);
            const mc::Vec3 b = mc::normalized(path[i + 2] - path[i]);
            if (mc::dot(a, b) < .995f) { targetSpeed = 12; break; }
        }
        for (size_t i = 0; i < game.vehicles.size(); ++i) {
            if (i == bikeIndex) continue;
            mc::Vec3 separation = game.vehicles[i].position - bike.position;
            separation.y = 0;
            const float ahead = mc::dot(separation, mc::forward(bike.yaw));
            if (ahead > 0 && ahead < 45 && std::abs(mc::dot(separation, mc::right(bike.yaw))) < 2.1f)
                targetSpeed = std::min(targetSpeed, std::max(0.0f, (ahead - 5) * .8f));
        }
        input.brake = bike.speed > targetSpeed + 1;
        input.moveY = input.brake ? 0 : mc::clamp((targetSpeed - bike.speed) * .45f +
            bike.speed * (.12f + .0055f * std::abs(bike.speed)) / 15.5f, 0, 1);
        const mc::Vec3 previousPosition = bike.position;
        const float previousYaw = bike.yaw;
        tick(game, input);
        const mc::Vehicle& driven = game.vehicles[bikeIndex];
        steeringTravel += std::abs(mc::wrapAngle(driven.yaw - previousYaw));
        peakPenalty = std::max(peakPenalty, game.harborSplit.penalty);
        require(finite(driven.position) && finite(driven.velocity) &&
                mc::length(driven.position - previousPosition) < 2,
                "control-driven trial produced invalid motion or a teleport");
        require(game.world.road(driven.position.x, driven.position.z),
                "control-driven trial left the city roads");
    }
    require(game.harborSplit.phase == mc::TrialPhase::Inactive && game.harborSplit.medal > 0 &&
            game.harborSplit.bestTime > 0 && game.harborSplit.bestTime < mc::Game::harborSplitLimit(),
            "actual throttle, brake, and steering inputs could not complete the timed course");
    require(game.occupied == int(bikeIndex) && game.vehicles[bikeIndex].health > 0 && game.health > 0 &&
            game.money > startingMoney && game.completedMissions == 0 && game.activeMission == -1,
            "control-driven finish lost its rider, reward, or campaign isolation");
    require(game.pedestrians.size() == population && game.vehicles.size() >= fleet &&
            population > 0 && fleet > 0, "control-driven trial removed the initialized living world");
    require(maxDeviation < 6 && steeringTravel > 10 && progress + 25 > path.size(),
            "control-driven trial skipped its road path or genuine turns");
    std::cout << "Harbor Split driven: " << game.harborSplit.bestTime << " s, peak penalty "
              << peakPenalty << " s, path deviation " << maxDeviation << " m, bike health "
              << game.vehicles[bikeIndex].health << '\n';
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
        {"movement sound follows displacement and contact", movementSoundMapping},
        {"vehicle interaction", vehicleInteraction},
        {"vehicle boarding respects walls", vehicleBoardingRespectsWalls},
        {"vehicle exit requires a clear path", vehicleExitRequiresClearPath},
        {"workshop bay repairs", workshopBayRepairs},
        {"workshop service boundaries", workshopServiceBoundaries},
        {"workshop counter first aid", workshopCounterFirstAid},
        {"workshop populated service visit", workshopPopulatedServiceVisit},
        {"workshop interior save round trip", workshopInteriorSaveRoundTrip},
        {"camera close walls and workshop sweep", cameraCloseWallAndWorkshopSweep},
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
        {"road campaign progression and rescue unlock", campaignProgression},
        {"delivery deadline and retry", missionDeadline},
        {"save round trip and corruption", saveRoundTripAndCorruption},
        {"version 1 save migration", legacySaveMigration},
        {"version 2 save migration", versionTwoSaveMigration},
        {"version 3 save migration", versionThreeSaveMigration},
        {"rescue contract and hold persistence", rescueContractAndHoldPersistence},
        {"survey contract flight and landing", surveyContractFlightAndLanding},
        {"craft contract failure and recovery", craftContractFailureAndRecovery},
        {"craft loans preserve occupied vehicles", craftLoansPreserveOccupiedVehicles},
        {"craft contract visuals survive load", craftContractVisualsSurviveLoad},
        {"Harbor Split start and objectives", harborSplitStartAndObjectives},
        {"Harbor Split failure and loan recovery", harborSplitFailuresAndLoans},
        {"Harbor Split ordered gates and medal rewards", harborSplitGatesAndRewards},
        {"Harbor Split collision penalties", harborSplitDamagePenalties},
        {"Harbor Split save interruption and corruption", harborSplitSaveAndCorruption},
        {"Harbor Split populated control-driven course", harborSplitDrivenCourse},
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
