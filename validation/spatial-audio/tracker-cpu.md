# Tracker CPU scope

`tracker_cpu_probe.cpp` advances the full 84-person/50-vehicle game and times only
`WorldAudioScene::update` from the current camera. Each morning/evening trace
discards 120 warmup frames and records 3,600 updates at a fixed simulation step
of 1/60 second. Game simulation, camera calculation, synthesis, streaming,
rendering and presentation are outside this timer.

The strict GCC 14.2 optimized run recorded morning/evening medians of
60.283/54.771 microseconds and 95th percentiles of 89.377/74.829 microseconds.
The tracker occupies 39,080 bytes on this Linux toolchain. Both traces reach
eight selected engines and two walkers, preserve the full population, and
reject no invalid, duplicate or over-capacity source data. Raw wall-clock
maxima are retained: 1.854/21.851 milliseconds. Other builds and tests were
running on this shared host; the long tail includes scheduling interference.
These are exploratory component timings, not hardware frame rates or a
Windows real-time guarantee. Source hashes were collected after the run.

```sh
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Werror validation/spatial-audio/tracker_cpu_probe.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o /tmp/meridian-tracker-cpu
/tmp/meridian-tracker-cpu
```
