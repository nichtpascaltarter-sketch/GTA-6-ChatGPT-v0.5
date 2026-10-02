# Police perception core validation

This archive covers the isolated law simulation and geometry adapter on base
`f8bebe0a58a8c4235b46213d8bc85537f03796e4`. It does not claim that the game executable
already uses this system. Existing Game, UI, audio, renderer, world, and save-file
sources were unchanged for this checkpoint. The exact tested files are listed in
`source.sha256`; `commands.txt` records compiler and build/run commands.

The strict and AddressSanitizer/UndefinedBehaviorSanitizer suites pass 12 law
scenarios and 8 real-World navigation scenarios. Leak detection is enabled and
both sanitizer runs report no diagnostics.
They exercise hidden-location invariance; audible and inaudible gunshots;
bounded deferred listener scans; observation aging and serial reuse; field of
view, cover, terrain and missing collision; delayed radio; distinct search
assignments; mixed patrol/officer behavior; command-driven detours and inserted
obstructions; 30/60 Hz firing cadence and magazine reload; pause; malformed
inputs; and validated state restoration with matching firing continuation.
The real-World route fixtures contain coordinate-correct collision residency;
movement follows returned commands and every resulting step is checked against
the standing-body corridor. No hidden-player position is accepted by LawFrame.

## Contract and limits

- Up to 16 stable logical units, 32 queued observations, and 16 search slots.
  Sight is checked against the observer's position, heading, 85/95 m range,
  collision residency, and line of sight. A 130-degree cone has a 3 m close
  awareness exception. Gunshots reach 100 m in clear space or 35 m through cover.
  Report events are already-validated dispatch reports.
- Only direct sight can authorize firing. Radio and sound may supply a movement
  objective but cannot supply another officer's visual exposure. Radio takes
  0.3 seconds. Unseen velocity prediction is capped at 0.5 seconds; memory expires
  after 45 seconds. Search slots are unique among active units.
- Each frame processes at most 8 events, 4 perception visibility checks,
  2 decisions, 2 movement corridor checks, and 8 objective projections. One
  incremental navigation job performs at most 12 geometry checks and 16 node
  expansions per frame. Job setup uses at most 5 additional projections. It
  stores at most 24 nearby obstacles, 98 nodes, and 32 path points. A failed
  setup can be attempted by at most the two decision slots in a frame.
- Officers aim for 0.75 seconds, fire at 0.70-second intervals, and reload six
  rounds in 2.4 seconds. At most two firing attempts occur per frame; each checks
  shoulder-to-muzzle and muzzle-to-observation clearance. Patrol units do not
  emit shots. LawShot contains a logical shooter, monotonic shot sequence, actual
  origin, direction, range, and damage request. Shot events last one update.
- LawState schema 1 is a value-only persistence contract. Serialize its fields
  explicitly; do not copy object bytes. Restore validates bounds, identities,
  memory ownership, search reservations, and weapon state. Current actors,
  queued observations, path caches, and presentation events are reconstructed.

## Game integration requirements

Use stable logical Law IDs independently of the runtime 64-bit Vehicle IDs.
Keep an explicit unit-to-pedestrian/vehicle-record association and resolve it
after load. Feed actual observer transforms and real sensory events; provide no
hidden-player input to movement or search decisions. A caller producing sight
samples must respect the perception budget, rotating observers as needed.

Apply movement requests through actual collision/vehicle dynamics; the law core
does not teleport actors. Patrol objectives still need the game's road-driving
controller. WorldLawSpace contains terrain and static solids; the game's final
shot resolver must intersect live vehicles, friendly officers, civilians, and
the player in distance order before applying damage. Muzzle events need separate
police presentation/audio lifetimes and must not reuse player shotFlash.

Preserve campaign, activity, resident, and earlier save migration behavior during
integration. Game getters agreed with audio are `lawState()` and `lawShots()`.
On load/device epoch changes, prime audio counters rather than replaying saved
shot counts. A paused or bypassed simulation update must clear old emitted shots.
Native rendering and complete-game acceptance belong to the integrated milestone.
