# World streaming CPU baseline

Snapshot: `88a2cf6e24f3c7037581142915f757d8a49fa5e0`. The runner obtains world.cpp, world.h, and mc_math.h from this Git revision into a temporary build directory; archived evidence includes no game-source copy or executable.

Host: Intel Xeon Platinum 8573C, Linux, g++14.2, `-std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic`. Each route runs as a fresh process pinned to CPU 0; routes run sequentially. This is a shared-host CPU benchmark, not GPU or player-device performance.

## Method

- Axial city: chunk centers x=-8 through 8, z=0, then reverse; three round trips, 96 measured boundary events.
- Diagonal city: chunk centers (-8,-8) through (8,8), then reverse; three round trips, 96 measured boundary events.
- Coastal aircraft: 5001 points along `World::coastalRoadPoint`, height terrain + 160 m, retain each successive changed chunk center; three forward/reverse passes, 510 measured boundary events. This models streaming footprint along a flight path, not flight physics.
- Each initial 49-chunk cold load is recorded separately and excluded from boundary percentiles. Each boundary calls `World::stream` followed by `combinedMesh`. Combined mesh allocation/copy is measured; its destruction is excluded. Mesh consumption checksum prevents dead-code elimination.
- Logical new chunks are computed from before/after coordinate sets. Existing implementation generates exactly those missing keys. All samples retain 49 chunks.
- Reported p95 uses nearest rank. RSS/HWM is read from `/proc/self/status` after combination while both chunk and combined meshes are live; HWM also captures earlier transient allocations. Capacity bytes include chunk vertex/index/solid/light vectors plus combined vertex/index vectors, excluding object/allocator metadata.
- One million unchanged-center calls are timed in each route stderr text file; those are excluded from boundary statistics.

|Route|Samples|New tiles|Stream ms med/p95/max|Combine ms med/p95/max|Sum ms med/p95/max|Peak RSS MiB|Max mesh capacity MiB|
|---|---:|---|---|---|---|---:|---:|
|city_axial|96|7|3.752/4.287/4.853|3.750/9.483/12.577|7.512/13.693/16.549|102.14|75.05|
|city_diagonal|96|13|6.983/8.198/12.933|3.733/4.803/11.654|10.731/14.155/18.774|105.82|75.05|
|coastal_aircraft|510|7|2.938/3.946/10.135|1.689/2.186/5.155|4.654/6.399/12.843|56.74|48.08|

## Cold loads

|Route|Stream ms|Combine ms|Total ms|
|---|---:|---:|---:|
|city_axial|35.418|13.039|48.456|
|city_diagonal|33.234|8.761|41.995|
|coastal_aircraft|21.648|3.414|25.062|

Maximum city combined mesh: 726,020 vertices and 1,026,744 indices, 33,147,776 bytes (31.61 MiB). This allocation copies all 49 resident chunks at every boundary, even when only 7 or 13 are newly generated.

## Rebuild and rerun

Run from this directory; the script prints a new temporary results directory and preserves these archived baseline files:

```sh
python3 run.py
```

Use `--revision COMMIT` to measure a later compatible World implementation, and `--output DIR` to select a results directory. The runner needs Python 3, Git, g++, and Linux `/proc`; it installs no dependencies. Raw per-boundary data is in the three CSVs; unchanged-center timing and checksums are in the `.stderr.txt` files. Timing depends on shared-host scheduling and CPU contention; compare revisions under similar host load rather than treating one run as a stable frame-budget guarantee. Environment details and affinity are in environment.json. Building from the recorded Git revision makes later working-tree changes irrelevant to this baseline.
