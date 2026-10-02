# Natural officer spawn obstruction, before repair

The root's natural capture experiment placed the player at `(-136,0,-86)` and
fired one real upward shot. Officer 0 turned toward the player but never acquired
personal sight or moved. This diagnostic preserves the full initial population:
84 pedestrians and 50 vehicles. It modifies only the player placement/view and
advances the ordinary simulation. No production code was changed for diagnosis.

All four natural officer starts fail `World::blocked(position,.35)` because they
overlap streetlight colliders. `Game::initialize` places patrol cars at road X
plus 3.2, Z plus 24; officers add 8 to X. The streetlights stand at road X plus 11
and Z offsets 24/64/104. The center separation is therefore .20m. Subtracting the
.12m pole half-width leaves .08m clearance for an actor with .35m radius.

For officer 0 at `(-116.800003,0,-104)`, chunk `(-1,-1)` solid 4 has bounds
`(-117.120003,.14,-104.120003)` through `(-116.879997,8.54,-103.879997)`.
The sight ray from eye height 1.6 to the player at height 1.1 hits this pole after
`0.109670408m`. The start cannot be projected into the navigation space, so no
movement command reaches the collision solver's existing recovery path.
Calling `World::move(position,{},.35)` independently would recover to
`(-116.527,0,-104)`. That call is diagnostic only; the test does not relocate the
officer. Nearby legal ground exists .5m north, south and east.

There is no dynamic blocker initially. After two seconds patrol vehicle 8 also
intersects the same sight ray at `9.829923m`, behind the pole. The pole is thus
the initial cause, rather than an erroneous dynamic line-of-sight rejection.

The production source hashes are the accompanying `source.sha256`; diagnostic
source has its own `obstruction-probe.sha256`. The archived log is the observed
**pre-repair** result. A later spawn correction should change this result.
Compile without a separate `game_law.cpp` object: the probe includes that
authored implementation to call the exact private collision functions.

```sh
c++ -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror -pthread \
  validation/police/visuals/obstruction_probe.cpp \
  src/game.cpp src/law.cpp src/law_navigation.cpp src/pedestrians.cpp \
  src/world.cpp src/world_geometry.cpp src/visuals.cpp \
  -o build/police-obstruction
build/police-obstruction
```

The recorded run reused the source-verified strict common objects from the
concurrent gameplay gate, excluding `game_law.o`, with the same flags above.
