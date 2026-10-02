#include "../src/game.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
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
    verifyMesh(game.dynamicMesh());
    verifyMesh(game.world.combinedMesh());
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

void refreshSaveChecksum(std::vector<char>& bytes) {
    require(bytes.size() >= 20, "save fixture has no version-1 header");
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
    setLittleEndian(corrupted, 56, 6); // Version 1 wanted level, beyond its legal maximum.
    refreshSaveChecksum(corrupted);
    reject(corrupted);
    corrupted = original;
    setLittleEndian(corrupted, 112, 0xffffffffu); // Version 1 vehicle count.
    refreshSaveChecksum(corrupted);
    reject(corrupted);
    corrupted = original;
    corrupted.push_back(0);
    reject(corrupted);
    require(!game.load(save.path.string() + ".absent"), "missing save file was accepted");
    require(!game.save(save.path.string() + "/file.sav"),
            "save reported success for an invalid directory");
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
        {"movement, pause, and timestep bounds", movementAndPause},
        {"vehicle interaction", vehicleInteraction},
        {"weapons and radio", weaponsAndRadio},
        {"shoulder aim and nearest hit", shoulderAimAndNearestHit},
        {"police damage respects solid walls", policeObstruction},
        {"complete campaign progression", campaignProgression},
        {"delivery deadline and retry", missionDeadline},
        {"save round trip and corruption", saveRoundTripAndCorruption},
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
