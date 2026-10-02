# Neighborhood routines gameplay validation

Validated on 2026-10-02 with `g++ (Debian 14.2.0-19) 14.2.0` on Linux.
Both strict and AddressSanitizer/UndefinedBehaviorSanitizer runs exited successfully:

| Suite | Strict | ASan + UBSan + leak detection |
| --- | --- | --- |
| Gameplay | 49/49 | 49/49 |
| Pedestrians | 9/9 | 9/9 |

`source.sha256` identifies the tested simulation, CPU mesh, test, and included header content. Run `sha256sum -c validation/routines/gameplay/source.sha256` from the repository root to verify it. Validation used base `fa34164` plus the pending routines implementation and tests; the hashes remain authoritative after those files are committed. Strict builds straddled only `fa34164`, which changed workshop cabinet literals `{170,175,180}` to `{170.0f,175.0f,180.0f}` to silence an MSVC warning; these are identical exactly representable coordinates. Both sanitizer builds consumed the final source content in the manifest.

The gameplay suite preserves existing campaign, vehicle, workshop, combat, saving, lighting, and CPU mesh regressions. It also checks v1-v4 migration, bounded v5 persistence, invalid records with recomputed checksums, seated targeting, and resident state after leaving the neighborhood. Authentic old population migration retained all 84 original people and states, assigning each an identity from its old index; a distant save retained all 84 and added 16 residents. During 150 seconds of subsequent simulation with all 50 vehicles and all people intact, all 16 residents travelled: 15 reached workplaces in the original-layout case and 16 in the distant-save case. Resident identities, bounded displacement, actual workplace arrival, and per-frame work budgets were checked throughout. The populated control-driven Harbor Split trial completed in 67.8333 seconds with no penalty and full motorcycle health.

The pedestrian suite covers physical work/seat/conversation arrivals, exclusive seat slots, interrupted groups and recovery, crossing waits and traffic yield, marked-road attachment, persistent residents and ambient recycling, pause, bounded dense-population work, sight/hearing reactions, actual sliding-vehicle velocity, and retaining a carried parcel through gunshot startle and flight. Natural resident-only probes reached all workplaces in 124.667 seconds and both conversation groups in 116.267 seconds. The dense runtime fixture uses 128 people; persistence intentionally rejects more than 100 in v5 and more than 84 in legacy formats.

These are portable simulation and CPU mesh checks. Windows/D3D12 capture and runtime evidence is recorded separately.

## Reproduction

Run from the repository root with Bash. These are the compile and run flags used for the archived logs:

```bash
common=(src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp)
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror tests/game_tests.cpp "${common[@]}" src/visuals.cpp -o /tmp/routines-release-game-tests
/tmp/routines-release-game-tests > /tmp/routines-release-game-strict.log 2>&1
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror tests/pedestrian_tests.cpp "${common[@]}" -o /tmp/routines-release-pedestrian-tests
/tmp/routines-release-pedestrian-tests > /tmp/routines-release-pedestrian-strict.log 2>&1

g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/game_tests.cpp "${common[@]}" src/visuals.cpp -o /tmp/routines-release-game-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/routines-release-game-asan > /tmp/routines-release-game-asan.log 2>&1
g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/pedestrian_tests.cpp "${common[@]}" -o /tmp/routines-release-pedestrian-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/routines-release-pedestrian-asan > /tmp/routines-release-pedestrian-asan.log 2>&1
```

No binaries or source copies are included in this evidence directory.
