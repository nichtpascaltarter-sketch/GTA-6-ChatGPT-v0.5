This compact archive contains probe sources, raw logs and source/binary hashes. Commands and relative paths below refer to the full scratch evidence root `/workspace/scratch/routine-visuals-paired`, where the frozen game-source snapshots and binaries are retained. No binaries or copied game-source trees are included in this compact directory.

# Pedestrian visual regression and geometry budget probe

`visual_probe.cpp` is an independently authored deterministic scene probe. It
constructs identical actors without advancing simulation and fingerprints every
vertex field and index. It reports 101 timed `Game::dynamicMesh()` calls after 7
warmups, including the result assignment/destruction. CPU times exclude world
streaming, gameplay simulation, renderer submission and GPU work. This is a
portable Linux generation microbenchmark, not a Windows or target-GPU result.

The immutable baseline source is commit `e7dc4f0` extracted in
`../routine-baseline-e7dc4f0/src`. `candidate-snapshot/src` is a frozen copy of the
routines worktree; all source files were hashed immediately before and after
copying, and both hash sets agreed. Its manifest is
`candidate-snapshot/source.sha256`. These complete source copies and executables
are scratch evidence only, not intended for the repository's compact archive.

Build from `/workspace/scratch/routine-visuals-paired`:

```sh
g++ -std=c++20 -O2 -pthread -I../routine-baseline-e7dc4f0/src visual_probe.cpp ../routine-baseline-e7dc4f0/src/game.cpp ../routine-baseline-e7dc4f0/src/world.cpp ../routine-baseline-e7dc4f0/src/world_geometry.cpp ../routine-baseline-e7dc4f0/src/visuals.cpp -o baseline
g++ -std=c++20 -O2 -pthread -DMC_ROUTINE_VISUALS -Icandidate-snapshot/src visual_probe.cpp candidate-snapshot/src/game.cpp candidate-snapshot/src/pedestrians.cpp candidate-snapshot/src/world.cpp candidate-snapshot/src/world_geometry.cpp candidate-snapshot/src/visuals.cpp -o candidate-frozen
```

The two moving-crowd scenes use the same positions, phases, colors, gait amplitude
and identity in each implementation. Their matching hashes verify the ordinary
walking path. The player-only, workshop, trial and rescue scenes verify existing
non-routine character paths without depending on live population scheduling.
Candidate-only mixed-activity scenes cycle all eight activities across 100 people
as a deliberately crowded geometry budget case. These artificial scenes exceed
the normal initialized population's close-range density; they are not gameplay
screenshots or a promise of dense-crowd frame rate.

`*-initial.log` was captured while other agents compiled. Its timing values are
only descriptive; geometry counts and hashes are deterministic. The first
candidate binary was built from the then-live worktree, so the reproducible
comparison must use `candidate-frozen` and the source manifest above.

The controlled `run_abba.py` pass pinned both immutable binaries to CPU 4. The
other agents held compilation/tests; no compiler process appeared at pass
boundaries and cgroup `nr_throttled`/`throttled_usec` stayed unchanged. Raw output
is in `abba.log` and A1/B1/B2/A2 logs. `abba-summary.json` reports means of the two
per-process medians, rather than presenting them as whole-frame or GPU timings.
All nine common scenes have identical mesh fingerprints. The close walking crowd
changed from 5.754393 to 5.757325 ms (+0.051%); the far crowd from 1.817167 to
1.828391 ms (+0.618%) in this run. These tiny differences do not establish a
meaningful performance change.

A later Work-only refinement adds an actual checking pencil, supporting hand and
downward gaze. `work-refinement-snapshot` reuses the same frozen dependencies but
replaces only `visuals.cpp`; its own `source.sha256` records that exact source.
`work-refinement.log` reports the updated geometry. Its CPU values are descriptive
because this follow-up was not measured in the coordinated idle window.
The mixed close crowd contains 196,659 vertices, 192,137 triangles and 10,172,004
bytes (+178,364 bytes over the walking-only baseline, of which +39,260 bytes are
the pencil refinement across 13 Work actors). The ordinary scene fingerprints
still match the baseline. `activity_fingerprints.cpp` independently checks each
of the eight activities at both distances before and after this refinement;
only Work is expected to differ.

Final Work contact tests check a pencil tip on the paper surface and downward eye
position along with both character detail levels. The base and final test logs,
including sanitizer output when present, are retained here. Exact test build:

```sh
cd work-refinement-snapshot
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Wshadow -Werror tests/pedestrian_visuals_tests.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o ../work-visual-tests
../work-visual-tests > ../work-visual-tests.log
g++ -std=c++20 -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror tests/pedestrian_visuals_tests.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o ../work-visual-tests-sanitized
ASAN_OPTIONS=detect_leaks=1 ../work-visual-tests-sanitized > ../work-visual-tests-sanitized.log
```

Build the per-activity probe in both `candidate-snapshot` and
`work-refinement-snapshot`:

```sh
g++ -std=c++20 -O2 -pthread -Isrc ../activity_fingerprints.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o ../activity-before
../activity-before > ../activity-before.log
# Use activity-after / activity-after.log for work-refinement-snapshot.
```

Final gaze repair `50d0d8b` changes only the Work pitch constant from .78 to .95 radians and strengthens the geometric test. Actual emitted eye/pupil centers now intersect the paper at four times through the idle sway. Final strict tests and all 14 non-Work activity/detail fingerprints pass; their logs are included as final-* files. The final source snapshot is retained in `/workspace/scratch/routine-gaze-final`. The sanitizer log covers c614d20's head rotation and pencil geometry; the final scalar pitch/test change was verified by the stricter geometry test, not a separately repeated sanitizer build.

A subsequent review reran the complete final pose suite with `50d0d8b` visuals and the `161d086` bench world change under ASan/UBSan (`-O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=1`). It passed, including the final gaze ray/page tests. The captured output is `final-visual-tests-asan.log`; this supersedes the earlier note that only the pre-gaze scalar had sanitizer coverage. The run linked the routines worktree's then-current gameplay, pedestrians, world, world_geometry and visuals translation units.
