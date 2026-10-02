# World audio tracker validation

`run.sh` builds and runs `tests/world_audio_scene_tests.cpp` twice: strict C++20 (`-O2 -Wall -Wextra -Wpedantic -Werror`) and AddressSanitizer/UndefinedBehaviorSanitizer (`-O1 -fsanitize=address,undefined`, leak detection enabled). The command uses only the C++ toolchain and repository source. It writes binaries under ignored `build/world-audio-validation`.

The six test groups cover actual displacement plus gait landings and interpolated contact age; pause/resume, stationary, seated, airborne, dead and implausibly fast contact suppression; source discontinuities, residency loss and load epochs; nearest eight sources, deterministic reorder continuity, stereo pan and smooth distance cutoffs; wooden piers over water and swimming; bounded histories and zero allocations across 1,000 updates; and unchanged save bytes with runtime vehicle identity assignment, copied records, traffic recycling and replacement loans. A nine-walker test passes real tracker snapshots into `WorldSynth` and verifies that nearest-source turnover and retained-tail readmission do not replay contacts.

The tracker occupies 39,080 bytes with this compiler; a source assertion caps its size below 64 KiB. Histories are bounded to 256 engines and 128 people; output is bounded to eight each. Excess input and duplicate identities are counted. Normal chunk/world render revisions do not reset identities. Camera discontinuities and explicit resets renew the audio epoch. Suspension is a fresh `update(..., 0, false, worldEpoch)` snapshot, preserving engine identity while breaking contact continuity.

`game-tests.log` records the separately run 49-test gameplay regression suite. Its command was:

```sh
c++ -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror -pthread tests/game_tests.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o build/audio-tracker/game_tests
./build/audio-tracker/game_tests
```

That suite exercises the unchanged player `movementAudio` helper and game identity changes; it does not instantiate the new world tracker. The final tracker-only tiny-delta bound refinement was validated by `run.sh` afterward.

These are portable simulation/DSP checks, not evidence of Windows endpoint behavior, acoustic quality or target-GPU performance. Native listening remains unverified. Independent read-only reviews of tracker/DSP lifecycle and vehicle identity lifecycle found no remaining blocking defect before the final gate.

Final result: all six tracker groups passed in both strict and ASan/UBSan builds, with leak detection and no sanitizer diagnostics. The first six PASS lines in tracker-tests.log are strict; the next six are sanitized. All 49 gameplay regressions passed.
