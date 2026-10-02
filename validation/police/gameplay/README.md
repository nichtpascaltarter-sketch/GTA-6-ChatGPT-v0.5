# Police Game integration

This checkpoint connects the observation-driven law core to the actual Game
simulation. Native presentation and complete executable acceptance are recorded
separately by the parent milestone; this directory contains portable gameplay
verification and its exact source provenance.

The final source passes 92/92 checks in both the strict GCC build and the
AddressSanitizer/UndefinedBehaviorSanitizer build with leak detection enabled:
14 police integration, 49 gameplay, 9 pedestrian, 12 law-core, and 8 navigation
checks. No sanitizer diagnostics occurred. All 23 source, header, and test hashes
match before and after these runs. Gameplay source is committed as `901f56f`;
the manifest also identifies the exact audio and visual source used by the gate.

## Behavior

The first four officers and first four police road vehicles have stable logical
Law IDs. Runtime vehicle audio identities remain 64-bit values with explicit
associations; they are never truncated into Law IDs. An occupied patrol is
unavailable to police control. Police vehicles are not recycled around an unseen
player.

Sight samples enter Law only after current observer angle, range, terrain,
static geometry, vehicles, and live people permit observation. The core checks
them again. Gunfire is an actual-origin sound clue with bounded hearing and
delayed radio relay. Patrol theft/damage transponders, surviving officer radio
reports, and the campaign's intercepted call supply explicit frozen dispatch
locations. Other collision crimes require a law witness. An externally changed
wanted level supplies alert severity without inventing a suspect location.

Officers follow collision-checked commands, preserve body spacing, and detour
around live vehicle obstructions. Small pre-existing static overlaps are
corrected before actions, with a one-metre bound and without discarding queued
evidence. Legacy positions remain unchanged during loading itself. Patrols route
remembered objectives along roads. Each emitted shot is resolved at its actual muzzle against the nearest
terrain/static hit, living body, or oriented vehicle volume. Interposed people
and vehicles inhibit aim/fire; a shot that still intersects an object stops
there. Occupied vehicle bodies absorb bullets before their occupants. The old
distance-based continuous police damage is removed.

`lawState()`, `lawShots()`, and `policePose(index)` supply read-only state and
actual events to presentation. Paused updates and loads clear emitted shots.
Clinic recovery and live officer replacement preserve the logical shooter's
monotonic shot counter so audio cannot replay an earlier event.

## Save version 6

The existing version-5 field order is preserved. A Law section follows the final
pedestrian route. All values are explicitly serialized little-endian; no object
representation or runtime pointer is saved. Runtime vehicle IDs are replaced by
validated vehicle-record references.

The section has an 88-byte header and one 192-byte record per Law unit. A memory
record is 48 bytes: four u32 values (valid, kind, serial, observer), position and
velocity vectors, then age and uncertainty floats. The header stores schema,
unit count, last processed evidence serial, wanted level, alert time, shared
memory, search mask, decision/weapon cursors, producer evidence serial, and
observer cursor. Each unit stores its identity/type/phase, home and goal, goal
flag, personal and pending-radio memories, five timers, search assignment,
weapon state, and its associated actor record index.

Loading validates finite bounds, canonical actor membership, distinct logical
identities and bindings, memory ownership, search slots, weapon state, counters,
and agreement with the main wanted/timer fields before replacing the live Game.
Versions 1–5 retain their population/progression migration and initialize police
with the saved alert severity but no invented knowledge of player location.
Routes, current candidate observations, presentation flashes, and audio events
are rebuilt instead of restored.

## Regression scope

The dedicated integration suite covers real visual acquisition and hidden-player
invariance; heard/unheard gunfire; discrete damage tied to emitted shots; live
friendly/civilian/vehicle obstruction; occupied-hull absorption; stolen patrol
control; pause/load event clearing; version-6 knowledge/cooldown persistence;
versions 1–5; and CRC-correct malformed Law data.

Three regressions were demonstrated before repair: officers could move through
one another while sharing a sighting; a forged fifth-patrol binding loaded
despite being outside the canonical four-vehicle roster; and the original four
officer starts overlapped authored streetlamp collision. The final tests require
natural officer separation with real front-rank firing, atomic rejection of the
forged binding, clear full-population starts, and a real initial shot after legacy
overlap recovery. Pre-repair evidence is labeled separately from final runs.

The complete gameplay and pedestrian suites remain in the gate, including the
full-population Harbor Split control route and natural resident work/social
arrivals. The original law-core and real-World navigation suites are rerun against
the integrated source. Commands, final hashes, and run logs accompany this file.
