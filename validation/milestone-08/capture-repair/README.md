# Resident inspection camera repair

Native Release source `c90828a` passed its process, image-format and simulation
checks, but visual inspection rejected `residents-work`: market goods completely
hid the worker and clipboard. The camera tested world collision boxes and dynamic
triangles. The visible goods had no collision boxes, so that test was incomplete.

The repaired search also tests rendered detail triangles, with chunk bounds as
a broad phase. It keeps a 2 cm endpoint allowance for static surfaces rather than
the 40 cm allowance used for the inspected person's own dynamic body and props.
No resident position, behavior, activity, reservation or pose is changed. The
capture log now includes the chosen eye and target for reproducible inspection.

The strict portable probe reaches all six activities naturally and checks their
new static visibility. A separate rendered box with no collision solids verifies
that noncolliding scenery blocks a sightline while an unobstructed ray stays
clear. The chosen Work camera is `(86.8746, 1.8, 38.0227)`, looking toward the same
worker at `(85.3323, 1.15, 34.9926)`. Native visual acceptance still requires the
new Windows capture; this portable result alone does not establish it.

The probe was compiled and run before being copied into this archive; its exact
contents and compiler inputs are hashed in `source.sha256`. Source hashes were
collected after the run. Reproduce from the repository root:

```sh
g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Werror -Isrc validation/milestone-08/capture-repair/probe.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o /tmp/meridian-resident-capture
/tmp/meridian-resident-capture
```
