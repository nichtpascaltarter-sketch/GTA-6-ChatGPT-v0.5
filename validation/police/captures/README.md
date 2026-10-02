# Natural police inspection views

The production `PoliceCapture` helper prepares three native views by advancing
the normal 84-person, 50-vehicle game. It places the player on a clear approach,
fires one actual upward shot, and waits for the first officer to aim, emit a
shot, or reload after six shots. It never assigns an officer position, pose,
health, wanted level, weapon clock, or ammunition count.

On the combined market/police source, the officer naturally moves to
`(-116.523, 0, -102.367)`. Aim, first fire, and reload captures settle at 1.13333,
1.3, and 5.65 simulated seconds respectively. Player health is 100, 92, and 52;
the captured emitted-shot counters are 0, 1, and 6. Static geometry and dynamic
mesh visibility checks find a clear camera for the head and torso.

The strict probe passed with the source snapshot in `source.sha256`:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
  validation/police/captures/probe.cpp \
  src/game.cpp src/game_law.cpp src/law.cpp src/law_navigation.cpp \
  src/pedestrians.cpp src/visuals.cpp src/world.cpp src/world_geometry.cpp \
  -o /tmp/meridian-police-captures
/tmp/meridian-police-captures
```

Eight repeated mesh/light preparations preserve each captured pose. The probe
also checks that an accidental paused simulation update invalidates the aim
and firing views, because ordinary pause deliberately clears aiming commands
and transient shots. The native inspection loop skips that update and checks
pose retention before every frame. Normal gameplay pause behavior is unchanged.

This is simulation and camera evidence. Actual D3D12 rendering of these three
views remains pending the combined native build.
