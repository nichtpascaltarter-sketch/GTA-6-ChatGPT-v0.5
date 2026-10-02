# Market capture integration

The market source was rebased onto the complete resident/spatial-audio source
`6faf9dc` without changing its geometry or focused test source. The duplicate
resident-camera repair was already present and was dropped during rebase.

`MarketCapture` supplies five named views to the actual application. The
integrated probe uses that production setup and camera table, then checks the
28 authored landmark/product surface samples from the original composition
probe. Every sample is inside the 68-degree, 16:9 view and clear of actual static
and dynamic triangles. All four market lights are selected at night. Setup
preserves all 84 people, 50 vehicles, their positions and identities; it changes
only the spectator, clock, weather and pause state. Unknown scene names fail.

The strict optimized probe passes. Source hashes were collected after execution;
no compiler inputs changed during its run. Native image inspection remains a
separate required gate. Reproduce from the repository root:

```sh
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Werror -Isrc validation/tide-hall-market/integration/view_probe.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o /tmp/meridian-market-views
/tmp/meridian-market-views
```

This batch also changes exactly representable test speed literals from integer
to float (`parked ? 0.f : 8.f`), resolving the two MSVC C4244 diagnostics found in
the initial resident native run. It does not change the test values or gameplay.
