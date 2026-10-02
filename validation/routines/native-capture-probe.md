# Natural resident capture validation

Six native inspection scenes use `ResidentCapture` to advance a fully initialized game with 84 people, 16 persistent residents, normal traffic, and an advancing clock. No civilian position, activity, pose, path, or reservation is assigned by the helper. Only the time of day and the player spectator placement are selected for inspection.

Carrying and fleeing require actual displacement and active motion. Work, sitting, and conversation must arise from normal decisions and persist for a stable window. Both alarm scenes first wait for a conversation, emit a real player gunshot, verify ammunition and muzzle feedback, then require that same calm resident to startle or flee. The camera tests each subject's head and torso, held props, static collision, and the final visible dynamic mesh. Native screenshots still require visual inspection.

The strict portable probe passes all six scenes, unknown-scene rejection, and interruption handling. The final rebased run records matching source hashes before compilation and after execution, its exact source commit and generated binary hash, and unedited output. These are simulation and camera-preparation checks, not native rendering or performance evidence.

Reproduce from the repository root:

```sh
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Werror -Wshadow -Isrc validation/routines/native-capture-probe.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o /tmp/meridian-resident-captures
/tmp/meridian-resident-captures
```
