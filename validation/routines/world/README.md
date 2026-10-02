# World and resident validation evidence

This archive contains focused development checks for the neighborhood routines milestone. It does not replace the final portable, sanitizer or native validation runs. Original probes and logs are retained without executable binaries or duplicate implementation files. `provenance.json` records their original locations and SHA-256 hashes.

| Check | Result | Evidence |
| --- | --- | --- |
| Navigation | 54 connected places, 252 nodes, 369 edges, 24 marked crossings; 15,104 bytes graph capacity | [Benchmark](navigation/benchmark-results.txt), [original probe](navigation/navigation_benchmark.cpp) |
| Navigation construction | 32-build origin workload: median 2.051 ms, p95 2.251 ms, maximum 2.350 ms | [Source hashes](navigation/source.sha256); world commit `8e69607` |
| World and bench collision | All eight seat positions allow front/side shots and 20 cm camera sweeps; seat/back/legs still block correctly; 9,987 navigation samples clear | [World output](bench/world-tests.log), [old-collider failure](bench/world-tests-before.log); fix/test commit `161d086` |
| LOD and workers after collider repair | Strict LOD geometry, LOD streaming and worker suites pass; source hashes stable across each run | [Summary](bench/summary.txt), individual JSON records and build/run logs |
| Persistence review | Invalid seat, duplicate home, unreserved route and resident nonedge route reject; unloaded route safely replans on return; blocked crossing waits; dead activity states roundtrip | [Final output](persistence/final.txt), [original probe](persistence/persistence_review.cpp) |
| Legacy migration review | 20 cases across versions 1–4 preserve original actors and unique identities; all produce byte-exact v5 roundtrips | [Output](migration/results.log), [original probe](migration/migration_review.cpp) |

The migration modes are: 0 original central population (16 reused, 84 total); 1 remote population (16 added, 100 total); 2 all original actors dead (16 added without healing originals, 100 total); 3 four-police fixture (16 added, 20 total); 4 partial central residency (8 reused plus 8 added, 92 total). Legacy 85-person and current 101-person saves reject. Tiny legacy fixtures below four people intentionally retain their original population without adding resident roles.

Origin geometry remains 347,741 triangles after the bench repair. The coarse cache remains 54,597,680 bytes in the real-world streaming test. Bench collision uses the same four part descriptors as the visible mesh, replacing the single box that previously filled visible empty space.

The persistence and migration source manifests were captured after each focused run against uncommitted gameplay work. They identify the reviewed source snapshots but are not a before/after compiler-input stability check. Earlier persistence logs show defects before their repairs; the archived final probe contains additional cases added later. Intermediate navigation optimization timings likewise have no independent source snapshots. These limitations are preserved rather than presenting the checks as final release certification.

For reproduction, the original C++ probes keep their original absolute include root and temporary fixture paths. Copy a probe into a temporary directory and replace `/workspace/GTA-6-ChatGPT-v0.5-routines/src/` with the target checkout's absolute `src/` path. From that checkout, compile navigation with `-std=c++20 -O2` plus `src/world.cpp src/world_geometry.cpp`. Compile either review probe with `-std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror` plus `src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp`. The bench runner retains exact original paths; its JSON logs give the complete commands for each repository test. Use the recorded source commits or snapshots when comparing historical results.

The follow-up MSVC warning cleanup is commit `fa34164`: cabinet position literals are now explicitly floating point. Strict syntax checking passed; the values and generated geometry are unchanged.
