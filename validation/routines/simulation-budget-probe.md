# Resident simulation work and local CPU cost

The probe runs four independent complete new games, each with 84 people and 50 vehicles. It records the first update separately, warms 120 more frames, then measures 3,600 updates at a 1/60-second simulation step. Scenarios begin at 08:00, 12:00 and 18:00; the fourth begins at 08:00 and emits a real player gunshot every five simulated seconds. Time and all people continue naturally. No civilian state is assigned.

Every measured update asserts at most eight decisions, two route searches, 512 route expansions and eight sight checks. Timing covers `Game::update` with world streaming disabled, matching that part of the main loop. It excludes initialization, external world streaming, mesh generation, audio, rendering and presentation. Output reports observed work peaks and microseconds per update.

This is a local exploratory run while other agents compiled or tested. It is not a controlled before/after comparison or an RTX frame-rate measurement. The accompanying JSON identifies the measured binary and source hashes captured after execution; it does not claim a before/after compiler-input immutability check. Native validation remains a separate gate.

Reproduce from the repository root:

```sh
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Werror -Isrc validation/routines/simulation-budget-probe.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o /tmp/meridian-simulation-budget
/tmp/meridian-simulation-budget
```
