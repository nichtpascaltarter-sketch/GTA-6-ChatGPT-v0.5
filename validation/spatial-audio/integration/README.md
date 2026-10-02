# Live population audio integration

The production main loop now publishes world audio from the final camera before
world uploads, mesh generation and rendering. Focus loss and minimization publish
a suspended snapshot before waiting for a message; successful loading advances
the world epoch and clears source histories. Camera overrides, cinematics and
map/menu suspension share the same source tracker.

`tests/world_audio_integration_tests.cpp` exercises the real initialized game,
the bounded source tracker, and the production synthesizer at 48 kHz. It advances
84 people and 50 vehicles, suspends for 30 publications, resumes, then saves and
loads into a fresh audio epoch. Across 843,200 generated audio frames it requires
finite bounded output, audible nearby sources and contacts, silence after
suspension, unchanged contact counts while frozen, and no replay on resume or
load. It also checks population preservation and unique source identities.

The strict and address/undefined-behavior sanitizer runs pass. Their recorded
Linux outputs agree: up to six selected engines and two walkers, 1,616 detected
physical contacts and 52 played strikes. Contacts include unselected histories;
first admission and source turnover deliberately consume old contacts, so those
counts should not be equal. The output fingerprint is evidence from this toolchain,
not a cross-platform golden value. Source hashes were collected after these runs.

Reproduce with GCC and the standard library only, from the repository root:

```sh
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Werror tests/world_audio_integration_tests.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o /tmp/meridian-live-audio
/tmp/meridian-live-audio
g++ -std=c++20 -O1 -g -pthread -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/world_audio_integration_tests.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o /tmp/meridian-live-audio-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/meridian-live-audio-asan
```

These tests generate samples without a Windows endpoint. They do not establish
native device playback, subjective sound quality, Windows scheduling deadlines,
or physical speaker/headphone behavior. Native source compilation and game launch
validation are a separate gate.
