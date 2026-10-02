# Police gunfire audio validation

Actual `LawShot` emissions now feed a bounded spatial transient path separate
from the player's muzzle-flash sound. The game tracker publishes every officer's
logical identity and monotonic weapon counter, including officers outside the
audible radius. Only a matching emitted shot supplies a retained muzzle origin;
a counter increase alone never manufactures a sound.

The fixed game tracker retains at most 16 latest emissions for 450 ms. The audio
side has 16 counter records and eight audible voices. New voices require an
actual emission no older than 120 ms; full banks consume and omit excess events,
with loudness and identity providing deterministic admission order. The current
700 ms weapon interval exceeds the retained event lifetime. A compile-time
check requires an explicit event-ring redesign if that assumption changes.

Sound remains at the actual emission origin. The final camera supplies the
listener, equal-power pan, a 220 m smooth cutoff and a 20 m distance reference.
Each sound combines independently seeded noise, a short bright crack, a low
resonant body and a decaying tail. Coefficients are computed outside rendering,
and the existing generated sine table is reused. No assets, dependencies,
per-sample transcendental calls, dynamic allocation or shared player/radio
random stream are introduced.

First admission, re-admission, epoch changes, stale recovery and device priming
consume the current counters without replay. Missing, malformed, inaudible and
capacity-dropped events also advance the high-water mark. Counter rollback does
not lower it. Pausing discards retained game-side emission payloads and releases
audible voices over 20 ms; repeated paused publications do not restart that
deadline. Known shooters may emit genuinely new shots on the first resumed
simulation step. Baseline-only rosters still advance the audio freshness clock.

## Reproduction

From the repository root with GCC 14.2 and the system C++ standard library:

```sh
g++ -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror tests/world_gunfire_tests.cpp -o /tmp/world_gunfire_tests
/tmp/world_gunfire_tests
g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer -fsanitize=address,undefined tests/world_gunfire_tests.cpp -o /tmp/world_gunfire_sanitized
ASAN_OPTIONS=detect_leaks=1 /tmp/world_gunfire_sanitized
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -x c++ -fsyntax-only -include src/audio_scene.h -include src/world_synth.h /dev/null
```

The focused suite covers real/missing emissions, old or malformed payloads,
distance/pan, every priming boundary, sequence rollback, duplicate identity
recovery, repeated pause release, retained delivery across a skipped snapshot,
age bounds, capacity overflow, source permutations and render block partitions.
It instruments allocation-free tracker/update/render/sample-rate changes and
exercises 8/44.1/48/96/192/384 kHz. `portable-release.txt` and `sanitized.txt`
contain the complete results. Strict conversion/shadow syntax checks also pass
for the tracker and world DSP headers.

`world_gunfire_integration_tests.cpp` additionally drives the actual Game law
system through the world tracker and mixer, checking emitted muzzle origins,
a deliberately skipped shot publication, focus-style suspension, separation
from the player flash, and save/load without replay. It links the same game and
world sources as the existing world audio integration suite, including
`game_law.cpp`, `law.cpp` and `law_navigation.cpp`.
The strict and ASan/UBSan runs both accepted all five real emissions exactly
once, including the intentionally skipped first emission publication. Their
results are in `integration-final.txt` and `integration-sanitized.txt`. Both were
linked against clean shared Game objects after the police observer,
movement-spacing, version-six binding-loader and initial spawn-recovery fixes
in gameplay revision `901f56f`. `source-sha256.txt`
identifies the tested source snapshot. The two previous scene suites were also
relinked against those final strict objects; their outputs are in
`existing-scene-final.txt` and `existing-integration-final.txt`.

## Exact baseline and performance scope

`audio_gunfire_baseline.cpp` compiled against frozen audio revision `7db152e`
and this source produced identical 3,087,840 float samples / 12,351,360 bytes.
This comparison includes all existing world engines/feet, source churn, epochs,
radio, weather, player contacts/shots, pauses and six device rates. The SHA256
of each raw stream is:

```text
528f361d4442598da9a7306dc5c68f6c7a7e537a22bca6f9bb6dd523d2887ac5
```

The original empty-world comparison also remains identical: 2,319,840 samples,
SHA256 `0af02e039470d8a75a07d2828e362df41aef4164109ddc7cc2c1afb4739a485d`.
Existing audio, world audio and audio output focused suites pass unchanged.

The release benchmark includes full radio/weather/player sounds and peaks of
eight gunfire voices, 12 engine voices, 12 walker voices and 36 contact tails in
64-frame render chunks. Recorded process CPU usage was 165.334 ms per three
seconds at 48 kHz (5.51% of one CPU) and 1115.924 ms at 384 kHz (37.20%). These
are shared Linux Xeon measurements, not Windows audio deadline, listening,
visual-quality or RTX 4070 performance validation. Sanitized timings include
instrumentation overhead and are not performance results.
