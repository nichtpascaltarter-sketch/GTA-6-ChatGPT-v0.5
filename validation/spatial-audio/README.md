# Spatial audio DSP validation

This source milestone adds a bounded procedural world mixer. The game-side
entity tracker and final main-loop publication ordering are separate integration
work. It does not claim native listening, Windows device validation, or target-PC
performance. The verified comparison source is `38c5c73`; the worktree also
contains root's independent focus/minimize publication fix `33ed4f3`.

## Runtime contract

`AudioState.world` is a trivially copyable snapshot with up to eight engine and
eight civilian footstep sources. The game computes selection, distance and pan;
the audio thread receives only IDs, gains and synthesis parameters. A new
`publicationSerial` is a fresh tracker heartbeat, including stationary or
cinematic updates. Lower serials within an epoch are ignored; repeated serials
do not refresh the 250 ms stale-data limit. A new epoch re-primes the scene.

Engine IDs remain stable for one entity generation. Type changes on the same ID
retire its existing oscillator. A generation change introduces a new oscillator
through a 20 ms gain smoother. Twelve engine slots allow four outgoing tails;
full banks omit new arrivals until a slot retires instead of overwriting audible
state. The same twelve-slot limit applies to walkers, with three contact tails
per walker. Admission and accumulation are deterministic under input reordering.

Walkers must remain published between strikes. First admission, re-admission,
epoch changes, stale recovery and pause/simulation resume consume the current
strike serial without replay. Later serial advances trigger at most the newest
strike when its age is at most 120 ms. Unchanged serials never replay. Every
contact retains its material, strength, independent random stream and envelope.
The five authored surfaces use procedural impulses, filtered noise and resonant
tones. Engine harmonics and contact oscillators use a 2,049-value sine table
generated at construction, outside rendering.

All world DSP storage is fixed. Its random streams do not consume radio, weather
or player-contact randomness. The existing mixer is byte-identical with an empty
world snapshot. Paused silence still advances world freshness/retirement without
advancing the existing radio and ambience. The WASAPI consumer uses `try_lock`
for snapshot copying and reuses its previous snapshot if the publisher owns the
mutex. Endpoint reopening starts a fresh synth and does not replay old contacts.

## Checks

Commands from the repository root (GCC 14.2, Linux x64):

```sh
g++ -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror tests/world_audio_tests.cpp -o /tmp/world_audio_tests
/tmp/world_audio_tests
g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer -fsanitize=address,undefined tests/world_audio_tests.cpp -o /tmp/world_audio_tests_sanitized
ASAN_OPTIONS=detect_leaks=1 /tmp/world_audio_tests_sanitized
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror tests/audio_tests.cpp -o /tmp/audio_existing_tests
/tmp/audio_existing_tests
```

The focused suite checks directional gains, attenuation, first admission,
serial gaps, stale and lower serials, pause and resume, epoch migration,
re-admission during a retained tail, deterministic array ordering and arbitrary
buffer partitioning, material-tail preservation, transition discontinuities,
full banks, malformed input, and zero allocations inside update/render at
8/44.1/48/96/384 kHz. It reaches all 12 engine slots, 12 walker slots and 36
contact tails. An independent reviewer additionally exercised 216 extreme-input
cases and 1,050 changing snapshot schedules under ASan/UBSan.

`audio_empty_fingerprint.cpp` can emit raw generated float bytes with `--raw`.
Compiled once against the verified parent headers and once against this source,
all 2,319,840 samples / 9,279,360 bytes matched. See
`empty-world-comparison.txt` for the complete stream SHA256. This compares
changing radio, engines, five contact surfaces, weather, shots, gain and pauses
at five device rates.

`lookup-comparison.txt` compares world-mix output with a reference that changes
only `WorldSynth::wave` to `float(std::sin(Tau*phase))`. Across 879,840 samples,
maximum absolute output error was below 4.82e-7. The independent reviewer also
checked 827,795 table boundaries, neighboring floating values and interval
midpoints under sanitizers; maximum sine error was below 1.224e-6.

## Performance scope

`portable-release.txt` records three seconds per device rate, using 64-frame
render chunks, full radio/weather/player contact synthesis, source churn and
deliberately excessive contact bursts. The benchmark asserts that every engine,
walker and contact slot is exercised. Allocation instrumentation is enabled
during every update and render. It reports both wall time and process CPU time;
the host identifies as an Intel Xeon Platinum 8573C in a shared Linux environment.
These measurements establish a portable CPU baseline, not a Windows audio
deadline guarantee. Sanitized timings are validation overhead, not performance
results. WASAPI device removal, output formats, contention and audible quality
still require the later native integration run.

Known integration responsibilities: exclude the occupied vehicle from world
engines; mint a new ID on recycling; increment epoch on load; derive footsteps
from real grounded displacement and matching gait phase; publish before GPU
work and before blocking on focus/minimize; keep selected walkers present
between contacts. This commit does not modify game simulation, world streaming
or build drivers.
