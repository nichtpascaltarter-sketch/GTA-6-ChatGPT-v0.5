# Tide Hall Market — portable evidence

This change develops the existing origin plaza into one authored market landmark. It preserves its 54 resident destination records, every graph node and link, the eight seat surfaces, the Tide Hall tower, the fountain basin, the surrounding road and pavement boundaries, and all six naturally reached resident inspection scenes.

The owner is chunk `(0,0)`. Public site metadata is `World::marketSite()`: bounds `(15,0,15)`–`(113,27.7,113)`, marker `(81.5,0,54)`, fountain `(64,0,64)`, and tower `(44,0,98)`. No new gameplay or save state is introduced.

## Authored composition

- Three individually named stalls: Sun Peel has open citrus crates; Blue Kettle has tea tins, a shaped kettle and filled mugs; Hearth has bread trays with rounded and scored loaves. Goods rest on visible supports and the counters have matching collision.
- Three copper canopies share a steel support rhythm, with six grounded columns and triangular cantilever ties. A supported Tide Hall Market sign identifies the court.
- Four recessed, framed warm fixtures light the walking side of the arcade at night. Their lenses match the actual light positions; the original three shop sources and twelve street lamps are preserved.
- Three wind-bent copper sails replace the fountain's old crossbar sculpture inside the existing basin collision. Broad stone inlays identify the market approach, fountain ring and tower court without adding obstacles.

The market slots remain `(85.5,0,z)` and their approaches `(83.5,0,z)`, for `z=35,54,73`. Each counter has bounds `x=86.35..88.65`, `y=0..1.15`, and `z=center±2.4`. The canopy walking volume remains open; roof collision follows eight shallow slope bands instead of filling the space below it.

## Measured checks

| Check | Result |
| --- | --- |
| Resident catalog / navigation | Same measured baseline fingerprints; 54 places, 252 nodes, 369 edges, 24 crossings |
| Path and seat preservation | 19,343 clear samples, eight supported seats, every graph edge swept in both directions |
| Market player clearance | 714 samples with a 0.34 m radius; solid counters and roof clearances tested separately |
| Origin detail / medium / far triangles | 8,822 / 1,478 / 1,246 |
| Loaded 49-chunk detail geometry | 349,211 triangles; +1,470 from the 347,741 baseline; existing 350,000 cap retained |
| Origin detail / medium / far capacity bytes | 932,600 / 125,856 / 111,872 |
| Coarse cache at origin | 54,678,272 bytes; +80,592 from baseline |
| Largest sampled coarse cache | 56,682,928 bytes; below the unchanged 64 MiB cap |
| Terrain and road checks | 288 mixed-LOD seams; 19,800 coarse road centroids across 119 chunks |
| Existing gameplay / resident tests | 49/49 and 9/9 passed |
| Existing world, workers, LOD, pose, workshop-lighting checks | Passed |
| Naturally reached resident inspection scenes | All six passed with the root's triangle-aware camera repair |
| New market inspection views | Five views, 28 key surface samples inside the actual renderer FOV and clear of static/dynamic triangles; four market lights selected at night |
| Focused sanitizer run | Address, undefined behavior and leak checks passed |

`results.json`, the logs and `provenance.json` record the checks. The source manifests differ only in the root's committed resident-camera repair, cherry-picked as `7ede3bd` from `408490e`. All other tested inputs stayed unchanged. The pre-repair capture run is excluded; `resident_capture_repaired.log` records the separate run using the exact repaired header.

The strict focused test can be reproduced from the repository root:

```sh
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Werror \
  tests/market_tests.cpp src/world.cpp src/world_geometry.cpp -o /tmp/market-tests
/tmp/market-tests
```

For the focused sanitizer run, replace `-O2` with `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, then run with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.

## Native view integration

`cameras.json` and `view_probe.cpp` define the exact setup. Initialize the normal population, set the player to `(77,0,17)`, stream there, set pause, clear rain and message time, and set the chosen hour. Do not modify pedestrian or vehicle state. The probe uses the same 68-degree vertical FOV and 16:9 aspect as the renderer. It validates selected landmark/product surfaces, not every image pixel; native inspection remains necessary.

| Name | Eye | Target | Hour | Expected composition |
| --- | --- | --- | --- | --- |
| `market-day` | 28,15,30 | 65,3.5,68 | 14 | Elevated court overview with all three canopy fronts, header, tower and fountain sculpture |
| `market-night` | 37,3.8,26 | 75,3.5,57 | 23 | Lower court view with the four warm arcade pools and existing shop lighting |
| `market-citrus` | 81.8,2.35,31.9 | 87.35,1.38,35 | 14 | Three supported crates of citrus and their counter |
| `market-tea` | 81.8,2.35,50.9 | 87.35,1.38,54 | 14 | Tea tins, ceramic kettle and two filled mugs on a cloth |
| `market-bread` | 81.8,2.35,69.9 | 87.35,1.38,73 | 14 | Two trays of scored bread with visible support and counter edge |

This evidence covers portable simulation and geometry checks. Native day/night captures, rendering inspection and GPU performance validation remain part of the integration milestone; these tests do not establish image quality or Windows frame rate.
