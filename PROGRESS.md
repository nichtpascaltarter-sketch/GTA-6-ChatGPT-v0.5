# Meridian Coast development record

## Project charter

Build an original open-world action game toward and beyond the quality, content,
and scope shown in official GTA 6 footage. That benchmark remains the target;
the first playable implementation does not satisfy it. Continue improving the
lowest-scoring areas and do not treat any score as a completion condition.

The distributable is one native Windows x64 executable. All game code and content
are authored in this repository. Only C++ standard library, compiler/toolchain,
Windows SDK, and Windows system APIs are allowed. Rendering is exclusively D3D12;
DXR is used for hardware ray tracing. SDK DXC precompiles and embeds every shader.
MSVC `/MT` statically links the C/C++ runtime. Windows system DLL imports remain
necessary for Win32, D3D12, DXGI, WASAPI, and XInput; no adjacent DLLs or assets are
part of the game distribution.

## Working state — 2026-10-02

- Repository: `nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5`.
- Branch: `development/meridian-coast`, based on initial commit `76bbb11`.
- Linux execution environment; native MSVC/SDK/D3D12 validation runs in Windows
  GitHub Actions. No local Windows GPU or connected desktop is available.
- Latest verified executable: `3c25f7def4417a6ad22d95ca3b695ab01f4a710d`,
  copied to `/workspace/outputs/MeridianCoast.exe`; see Milestone 06 below.
- Native Release/Debug, system-import audits, 29 gameplay suites, 19 additional
  scene captures, and strict window lifecycle validation pass. WARP does not
  establish hardware DXR correctness or RTX 4070 performance.
- This remains a very early game, far below the full target. No claim of AAA
  quality, completeness, crash freedom, or 1440p/60 performance is made.

## Implemented source

- Handwritten math, meshes, collision, world geography, chunk streaming, and
  procedural buildings, street furniture, vegetation, water, roads, and islands.
- 12.288 km square world bounds; 49 loaded 128 m chunks. Geometry is generated
  deterministically with distinct district rules. Building families still repeat.
- Win32 application, raw mouse, keyboard, XInput, pause/title/settings interface,
  custom bitmap typography, HUD, minimap, fullscreen, saved settings.
- D3D12 depth-tested meshes and two fenced frame contexts; procedural atmospheric
  sky, daylight cycle, clouds, rain, distance fog, surface variation, and direct
  GGX/Schlick lighting. These effects are not dynamic global illumination or
  volumetric cloud simulation.
- Optional DXR 1.1 inline rays over streamed static geometry for sunlight
  occlusion and reflected surface hits. Dynamic actors are not in the ray scene.
- WASAPI output with original code-generated stereo radio stations, engine,
  wind/rain, sirens, and gunfire. No recordings or imported assets.
- MSVC/SDK one-command build, embedded DXIL, system-import audit, checksums,
  portable simulation tests, and Windows software-rendering smoke test.

## Current validation

- At the verified checkpoint the central 49 chunks contain 726,020 vertices and
  342,248 triangles. Countryside: 221,134 triangles; wetland: 294,430; island:
  130,916; coast: 140,226. Tests cover deterministic regeneration, collision,
  seams, 8,199 regional road samples, 14,014 road sweeps, lights, dock and runway.
- Native 29/29 gameplay suites pass, including runway takeoff, banking, stall,
  hard-landing damage across frame offsets, boat handling/swimming, save migration,
  passive aircraft motion, altitude-aware police visibility and dry exit preference.
  The six-contract campaign, conversations and persistent guidance are captured
  natively; movement-audio integration passes the 29th native and portable suite.
- Audio synthesis passes strict tests and ASan/UBSan, including four engine
  sounds, biome ambience, finite output, transition continuity and chunking.
- Archived `fd058a8` passed a 648-simulated-second sanitizer soak, 38,880 updates,
  152 mesh validations, 52 exact save/load round trips and 16 boundary cases.
  A craft-focused 570-second soak of `a56e058` also passed 34,200 updates,
  150 mesh checks, 114 light checks and 42 exact save/load round trips without
  sanitizer errors (peak RSS 331 MiB). Its additional landing probe exposed
  inconsistent hard-landing damage, repaired in `eb28a37` (22 strict gameplay suites pass): a fast descending plane
  within 10 cm of terrain was incorrectly treated as already grounded.
- Native audio listening, real controller hardware, clean Windows 10 coverage,
  hardware DXR, and 2560x1440 RTX 4070 frame-time measurements remain unverified.

## Active tasks

1. HDR/MSAA passed native Release and Debug validation, including actual 4x, 2x,
   and 1x resize/fullscreen paths. Evidence and scorecard are in Milestone 03.
2. Six-contract campaign validated in Milestone 04: Leena's offshore clinic rescue
   and the three-pass aircraft survey with runway landing, six cinematics,
   persistent objectives/countdowns and rescue/survey scene captures. Both new
   contracts completed control-driven portable probes with the full initialized
   population and no state edits after acceptance. Rescue: 183.867 seconds;
   full survey: 193.683 seconds, both with full craft/player health. Assertive
   probe sources and logs are committed in `validation/campaign-probes/`.
3. Root owns main/UI/map and milestone documentation; gameplay owns game/visuals/
   tests; world owns deterministic generation; build validation owns build/CI;
   renderer owns D3D12 resources/shaders. Complete snapshot commits precede sync.
4. Bounded asynchronous generation and persistent per-chunk GPU geometry/BLAS
   passed Milestone 04. The old detailed-only eight-phase streaming probe remains
   as a regression guard. A controlled A/B/B/A WARP comparison of combined versus
   per-chunk drawing completed all eight game launches at workflow `36948442197`.
   Its final summary writer failed after the measurements; original reports were
   preserved and the aggregation was repaired in `ad5b0d9`. Same-runner 120-frame
   medians are 26.157 s before / 26.3545 s after (+0.755%, below the old build's
   2.82% spread), with byte-identical captures at 8, 120 and 360 frames. This does
   not establish a material rendering regression; GPU frame timing is still needed.
5. Five original footstep responses, swim splashes and tire scrub are integrated
   and natively tested in Milestone 05. Shared sky/fog radiance repairs the distant
   color mismatch, and closer native car/boat captures verify the passenger.
6. Active distant coverage implementation: retain detailed 49 collision tiles;
   medium radius 7 and far radius 16 with radius 17 prefetch, shared deterministic
   descriptors and 8 m perimeter samples. Two bounded workers prioritize detail,
   missing coverage and refinement. A separate render revision and selected
   RenderTileView list keep exactly one representation per cell. Retain detail
   until coarse replacement exists; protect post-publication coverage during
   cache eviction. A saturated-cache regression reproduces the old hole and
   verifies its repair. Coarse cache cap is 64 MiB; detail fallback cap 64 MiB.
   Causeway height and geometry now follow island terrain consistently, and
   coarse road patches follow their actual rendered ground. Full integrated native Release/Debug validation passed in Milestone 06.
7. Integrated renderer work: bounds-based main/shadow culling, eligibility-aware
   TLAS retention, coverage-driven horizontal fog to 2 km, reversed scene depth
   with forward shadow depth, and ground-anchored shadows for high aircraft.
   Interactive first display must remain prompt; smoke coverage can prewarm with
   message pumping and bounded timeout. Preserve existing streaming regression
   with distant mode disabled. Root owns main/probes and milestone documentation.
   Source `3c25f7def4417a6ad22d95ca3b695ab01f4a710d` passed native workflow
   `36949428131`. The first run exposed a test expectation that omitted one-cell
   medium-LOD hysteresis; actual selected counts after an axial move are
   49/191/849. The corrected regression and all five native phases pass.
   Initial and repaired evidence remain under `validation/milestone-06/`.
8. Next candidate on `development/activities-and-timing`: original Harbor Split
   motorcycle trial, medals, payouts, save migration, HUD/map integration, and
   nonblocking GPU timestamp / CPU submission telemetry. All 36 gameplay suites
   and the complete portable suite pass. Rebased onto the verified milestone;
   run native Release/Debug, inspect the three trial captures and timestamp
   fallback/lifecycle path, then archive evidence and scorecard.
9. Subsequent content on `development/harbor-workshop`: a complete enterable
   Harbor Motor Works, shared site metadata, continuous forecourt, segmented
   collision, always-lit interior fixtures, bay repair and office treatment,
   boarding/exit obstruction fixes, contextual guidance and native captures.
   World, gameplay, renderer and root UI work are active in an isolated worktree.
10. Broader goals remain pedestrian routines, police tactics, authored districts,
    destruction, animation, true dynamic GI and volumetric atmosphere. Measure
    actual hardware frame times and compatibility as access becomes available.

### Current candidate (not yet the verified executable)

- LOD source `3c25f7d` passed native workflow `36949428131` and its evidence
  is archived. The aggregation-only comparison rerun is `36949428138`.
- The current candidate integrates Harbor Split and frame timing after the
  verified LOD milestone. Root owns the added HUD/map, F9 message preservation,
  three trial scenes and `timing_probe.h`; build validation owns build/CI and
  parser changes. The executable in `/workspace/outputs/` remains the last
  verified LOD build until this candidate passes native validation.
- Harbor Split passes 36 strict and sanitizer gameplay suites. Its full-population
  control-driven route clears nine 6 m gates in 67.833 seconds with full health
  and no penalties. Save v4 preserves records/paid medals and cancels unfinished
  attempts. Native visual verification remains pending.
- Timestamp telemetry uses four query slots, existing frame fences and a bounded
  120-frame history, with no new waits. Scope is GPU render commands; queued
  world uploads and presentation are excluded. CPU phases and world submission
  are reported separately. The next native suite requires matching completed
  samples and tests forced timestamp disable; historical comparison binaries
  retain compatibility because timing validation is optional in that tool.
- Harbor Motor Works is being implemented in the separate workshop worktree.
  Outdoor pedestrian schedules, shared threat reactions and small groups are
  planned afterward. Preserve the full world scope while extending these systems.

Captures for the verified checkpoint are in
`/workspace/scratch/lod-3c25f7d-release/`; machine-readable evidence and selected
original captures are under `validation/milestone-06/`. The full original target
remains unchanged.

## Initial scorecard — before native validation

Scores are against GTA 6 itself (10 = parity), not adjusted for team size or
dependency constraints. They are conservative provisional source-based ratings.

| Category | Score | Evidence |
| --- | ---: | --- |
| Map scale and variety | 1/10 | A 12.288 km procedural geography and seven biome rules exist, but authored content density and uniqueness are far below the benchmark. |
| Visual fidelity | 0.5/10 | Basic procedural meshes and a D3D12/PBR shader pipeline exist, with no inspected native frame yet. |
| World density and life | 0/10 | Population simulation has not yet completed integration and native validation. |
| Vehicles and driving | 0/10 | Vehicle implementation is in progress and no native driving session has been verified. |
| On-foot and combat | 0/10 | Controls are integrated at source level; gameplay has not been validated in the native application. |
| NPC and police AI | 0/10 | Source work is in progress with no integrated evidence yet. |
| Missions and story | 0/10 | The original setting is defined but mission integration is not validated. |
| Audio | 0.5/10 | Three original procedural music arrangements and effects pass signal tests; Windows device playback is unverified. |
| Performance and stability | 0/10 | Portable tests do not establish native stability or any hardware frame-rate target. |

## Resume protocol

Read this file and `git status`, then inspect the latest build results. Preserve
existing work. Finish the active validation/repair first, append evidence and a
scorecard at each major milestone, commit, and take the next active improvement.
Missing features belong in this record, not as code stubs or fake menu options.

This environment's Git transport can fetch but cannot authenticate pushes.
`/workspace/scratch/update_git_api.py` uploads exact committed Git objects and
fast-forwards the development branch through authenticated `gh api`; fetch the
branch first so its remote-tracking base is current. GitHub artifact CLI
downloads fail at their storage redirect; use the connected GitHub workflow
artifact download tool, then `download_file` on its returned file ID.

## Milestone 01 — native playable core, 2026-10-02

Validated source: `1bbac2e568636166d548d3f31b3fc353acfa72f7`.
[Windows build and launch evidence](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36944872811).
Machine-readable evidence is in `validation/milestone-01/`.

Release and Debug were built with the Microsoft x64 compiler and SDK DXC. Both
passed native world/game/audio tests and a 120-frame D3D12 WARP launch at 960x540.
The Debug run enabled the actual D3D12 debug layer and checked corruption/error
messages without finding any. Both captures have the same SHA256 and 889 sampled
colors. The Release executable is 551,424 bytes and imports only `d3d12`, `dxgi`,
`kernel32`, `ole32`, `shell32`, `user32`, and `xinput9_1_0` system DLLs.
Release SHA256: `287e7c97cdfde14a16fcf3ed237d152e61824a968daf69757ae2afd6f7a12b2a`.

Gameplay now includes walking, jumping, aimed gunfire, reloads, cars and
motorcycles, traffic, fleeing pedestrians, patrols with wall-aware visibility,
four completable original jobs, money, repair/ammunition purchases, and atomic
versioned saves. Ten gameplay tests pass, including complete campaign rewards,
invalid saves, shoulder-camera accuracy, and police wall occlusion. Initial
eight suites plus world/audio passed ASan/UBSan; the two later combat regressions
passed strict C++20 and native Windows tests.

The first native compile exposed a local `math.h` shadowing the standard header;
the include path was corrected and the header renamed `mc_math.h`. Review also
fixed overlapping controller actions, hidden distant minimap objectives, map
roads inconsistent with geography, and stale GPU geometry after loading.

Visual inspection: a coherent city, street, palms, walking characters, a car,
and legible HUD render correctly. Major weaknesses are block-shaped people and
vehicles, repetitive buildings, pale lighting, no raster geometry shadows, very
simple materials, and short fog-limited sightlines. Optional DXR is compiled but
has not run on capable hardware. Native audio, controller hardware, clean Windows
10 compatibility, and long-duration reliability have not been verified. A Linux
CPU-only 600-frame sample of simulation plus dynamic mesh generation averaged
0.729 ms (p95 0.853 ms, maximum 4.336 ms); this excludes rendering and is not a
Windows or GPU performance claim.

### Milestone 01 scorecard

| Category | Score | Evidence against GTA 6 |
| --- | ---: | --- |
| Map scale and variety | 1/10 | Twelve-kilometre procedural geography and several biomes exist, but the city visibly repeats a sparse block layout and has no dense authored interiors. |
| Visual fidelity | 0.5/10 | Native screenshots verify simple shaded geometry and atmosphere, far below cinematic character, material, lighting, and environmental detail. |
| World density and life | 0.5/10 | 48 vehicles and 84 pedestrians provide basic movement and reactions, without rich routines or ambient interactions. |
| Vehicles and driving | 0.5/10 | Cars and motorcycles accelerate, steer, brake, collide, and can be entered, but lack advanced suspension, handling, deformation, boats, and aircraft. |
| On-foot and combat | 0.5/10 | Movement, jump, aim, reload, hits, damage, and recovery work with tests, but animation and combat depth remain primitive. |
| NPC and police AI | 0.5/10 | Patrol pursuit, wanted decay, pedestrian panic, and occluded sight/damage exist, without tactical coordination or nuanced civilian behavior. |
| Missions and story | 0.5/10 | Four original jobs with sequential objectives, failure/retry, and rewards pass campaign tests, but have no cinematic scenes or broad campaign. |
| Audio | 0.5/10 | Three original synthesized stations and effects pass signal tests, but no native listening test, speech, or detailed acoustic world is verified. |
| Performance and stability | 0.5/10 | Two native 120-frame WARP runs pass with a clean debug layer; hardware 1440p/60, DXR, clean-PC coverage, and extended stability remain unmeasured. |

Next milestone: directional raster shadows, better curved character/vehicle
silhouettes, and multi-biome/weather image inspection. The full target remains
unchanged and this scorecard is not a completion claim.

## Milestone 02 — expanded world, craft and lighting, 2026-10-02

Validated source: `a56e0584d9fbb56aeb2dfe50e4eb92183b69737e`.
[Native build, execution and captures](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36946344981).
Evidence is in `validation/milestone-02/`. The self-contained Release executable
is 701,952 bytes; SHA256:
`25eebd77ede8a3e256d229dc4307a1258bf032dd345477ff893ba93fc7390a80`.
Only seven inbox Windows libraries are imported; shaders and runtime remain
embedded/static. Every smoke run copies only the EXE into an empty directory.

This checkpoint adds directional raster shadows; actual street/shop, headlight,
police and muzzle illumination; six building families and market architecture;
rounded character/vehicle geometry; authored palms, broadleaf trees, cypress,
mangroves, reeds and ground cover; a coastal pier and 512 m airstrip; boats,
swimming and fixed-wing flight; version 2 saves with version 1 migration; four
original subtitled cinematic conversations; map navigation/waypoints; craft HUD
and four synthesized engines with district ambience. The conversations are not
voiced. Local lights do not yet cast shadows; DXR still includes static geometry
only. Far views remain short and synchronous world uploads remain a bottleneck.

Release and Debug each passed four native test executables including 19 gameplay
suites, plus 120-frame WARP launches. Release passed 13 further scene captures.
Debug checked actual client sizes `976x657 -> 1024x768 -> 976x657` through resize,
fullscreen and restoration, exited after 120 frames, and reported no debug-layer
corruption/errors. Captures verify the new craft/vegetation, readable night
headlight pools, removal of facade banding, and readable minimum-window text.

Validation found and repaired sheared facade noise, a fivefold WARP slowdown
from barely emitting dusk lights, missing coastal-road map segments, seabed-based
waypoint distances, disappearing subpixel glyph rows, suspended unoccupied
planes, and police sight ranges that ignored altitude. The 120-frame Release
WARP launch step fell from 70 seconds to 15 seconds after light pruning; the final
repair run took 23 seconds on another hosted runner. These timings include
startup and are not a controlled GPU benchmark or a hardware performance claim.

### Milestone 02 scorecard

| Category | Score | Evidence against GTA 6 |
| --- | ---: | --- |
| Map scale and variety | 1/10 | A twelve-kilometre geography now has distinct vegetation, a pier and airstrip, but block layouts and sparse scenery still repeat heavily. |
| Visual fidelity | 0.5/10 | Native captures show functioning shadows and local lighting, but low-polygon people, simple materials, aliasing and short sightlines remain far below the benchmark. |
| World density and life | 0.5/10 | 48 road vehicles and 84 pedestrians plus two parked craft populate basic routes without rich routines, crowds or ambient events. |
| Vehicles and driving | 1/10 | Cars, motorcycles, boats and aircraft are playable, with tested buoyancy, swimming, takeoff and landing, but handling, animation, damage and interaction remain basic. |
| On-foot and combat | 0.5/10 | Aimed combat, collision, health and swimming work, with primitive animation and very limited combat depth. |
| NPC and police AI | 0.5/10 | Pursuit and occluded three-dimensional sight work with regressions, but there is no tactical coordination, investigation or nuanced civilian behavior. |
| Missions and story | 0.5/10 | Four original jobs and four brief subtitled scenes work, but campaign length, staging, acting and mission variety remain minimal. |
| Audio | 0.5/10 | Original stations, four engine types and biome ambience pass synthesis checks, but native listening, voices and spatial acoustics remain unverified or absent. |
| Performance and stability | 0.5/10 | Native software-rendering, debug lifecycle and extensive portable tests pass, while synchronous streaming, real GPU frame times, hardware DXR and long native sessions remain unresolved. |

Continue immediately with the active rendering milestone; none of these scores
is a completion condition.


## Milestone 03 — HDR and antialiasing, 2026-10-02

Validated source: `f70a8e99cdbb5af41c4ea3e6fd3c7bd42be7b1db`.
[Native build, execution and captures](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36946847879).
Evidence is in `validation/milestone-03/`. Release is 716,288 bytes; SHA256:
`ae0b8549486360b06959e766fecd86741f213ae50029b8f964f286534cc54602`.
Nine DXC shader variants are embedded; only the same seven Windows system
libraries are imported. The single-EXE isolation checks pass.

Scene lighting now renders into a linear floating-point HDR target, using
queried 4x/2x MSAA with 1x fallback, resolve, soft-knee bloom and tone mapping.
The HUD renders afterward and remains sharp. Native portrait, vehicle, night
and coast captures show smoother silhouettes without the earlier facade bands.
The 13-scene Release sweep and 120-frame launch pass. Debug enabled the actual
D3D12 validation layer and passed 120-frame resize/fullscreen/restore runs at
actual 4x, 1x and 2x, with no corruption/error messages. The aircraft exit and
hard-landing repairs pass all 22 native gameplay suites.

On hosted WARP runners, the 120-frame Release process took 23.767 seconds versus
21.398 seconds before HDR; night capture took 5.724 versus 5.654 seconds. These
are process launch-to-exit measurements on separate software-rendering runners,
not controlled GPU frame-time comparisons. Debug lifecycle elapsed times were
34.365, 30.014 and 32.340 seconds at 4x, 1x and 2x respectively. Real DXR hardware,
1440p/60, clean Windows 10, listening, and controller hardware remain unverified.

### Milestone 03 scorecard

| Category | Score | Evidence against GTA 6 |
| --- | ---: | --- |
| Map scale and variety | 1/10 | Large procedural bounds and several regions exist, but detailed residency is short-range and individual locations remain sparse. |
| Visual fidelity | 0.5/10 | HDR and antialiasing improve inspected native captures, while material complexity, animation, characters and lighting remain far below the benchmark. |
| World density and life | 0.5/10 | Basic traffic and pedestrians occupy the scene, without rich schedules, social behavior or broad ambient activities. |
| Vehicles and driving | 1/10 | Four craft classes are playable with tested takeoff, stall, landing and water movement, but handling detail and damage remain rudimentary. |
| On-foot and combat | 0.5/10 | Tested movement and aimed combat work, with limited animation, encounters, weapon variety and interaction depth. |
| NPC and police AI | 0.5/10 | Sight and damage respect walls and altitude, but pursuit behavior and civilian reactions remain simple. |
| Missions and story | 0.5/10 | The verified binary has four contracts and short original conversations; the expanded six-contract source still awaits native validation. |
| Audio | 0.5/10 | Original synthesis covers radio, four engines and ambience, but speech, rich acoustics and native listening are absent from verification. |
| Performance and stability | 0.5/10 | Release and three Debug MSAA lifecycle paths pass, while synchronous streaming stalls and unmeasured hardware performance remain substantial gaps. |


## Milestone 04 — six contracts and bounded streaming, 2026-10-02

Validated source: `2acd647a877b2062e79580c90c36da90eb4d1cdf`.
[Native build, streaming and capture evidence](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36947811515).
Release is 799,232 bytes; SHA256:
`911b0c5aa17930bdb92ee49c4a94bcbfb687eb03bc2cf52e7c016159efecf7d7`.
All shaders remain embedded, CRT static, and imports limited to the same seven
Windows system libraries. The updated executable is in `/workspace/outputs/`.

The campaign now has an offshore clinic rescue and a three-gate aircraft survey
ending in a runway landing, with authored conversations and persistent objective
instructions/countdowns. Versioned saves retain active rescue progress and read
older saves. Full-population input-driven probes complete rescue in 183.867 s
and survey in 193.683 s with full craft/player health and exact rewards. Review
found disappearing passenger visuals after changing vehicle; the repair keeps
Leena and a secured medical case in all four vehicle classes. Native captures
clearly show bike/aircraft passengers; car/boat inspection angles partly occlude
her, so closer angles are queued in the next capture set. Indexed geometry,
transforms and save/load tests pass, including sanitizer coverage.

World generation now uses two bounded workers, immutable results, stable tickets
and reset epochs. Atomic detailed publication preserves all 49 chunks; missing
nearby collision coverage triggers an explicit synchronous fallback. GPU arenas
retain unchanged chunks, build only incoming BLAS on capable hardware, rebuild
the small TLAS with per-instance index offsets, and retire resources by fence.
Eight-phase native routes in both configurations verify exact upload counts
49/7/7/13/25/49/49/49, retained geometry, background worker counts, zero normal
streaming waits, zero pressure/repack waits, bounded queues and complete drains.
Both builds pass world/worker, 28 gameplay, audio and cinematic tests. Debug runs
use the actual validation layer and pass all 4x/1x/2x window lifecycle transitions
without corruption/error messages. Release passes 19 additional scene captures.

CPU-only same-source route benchmarks measure median summed main-thread service
costs of 0.082/1.622/0.078 ms (axial/diagonal/coastal), compared with synchronous
world generation plus mesh copying at 7.457/10.735/6.019 ms. Publication latency
is 33.47/66.81/33.46 ms at 60 Hz; the prior complete neighborhood remains available
while results arrive. All 702 transitions per mode preserve collision coverage,
with zero unexpected fallback/rejection. Detailed methodology, raw data and
one-core contention controls are in `validation/streaming-async/`.

GPU speed is not established by these CPU results. The Release WARP city process
took 29.408 s versus 19.165 s for the prior campaign build on a different runner;
streaming took 27.872 s (Debug 29.398 s). A controlled same-runner comparison is
being prepared to separate runner variation and the cost of additional draw
calls. Hardware DXR, RTX 4070 at 1440p/60, native listening/controller testing,
clean Windows 10 compatibility and extended native stability remain unverified.
Short visibility and terrain coverage edges remain under active improvement;
shared sky/fog evaluation and a separate distant LOD layer are next.

### Milestone 04 scorecard

| Category | Score | Evidence against GTA 6 |
| --- | ---: | --- |
| Map scale and variety | 1/10 | A large procedural geography streams correctly, but detailed coverage remains short and regional content lacks the benchmark's authored density. |
| Visual fidelity | 0.5/10 | HDR, shadows and native passenger views work, while characters, materials, animation and distant coverage remain visibly primitive. |
| World density and life | 0.5/10 | Traffic and pedestrians coexist with tested campaign routes, without the benchmark's crowds, routines and rich incidental activity. |
| Vehicles and driving | 1/10 | Cars, motorcycles, boats and aircraft support full control-driven routes and passenger persistence, but handling and damage depth remain limited. |
| On-foot and combat | 0.5/10 | Core movement and combat pass regression tests; animation, encounters, cover and interaction breadth remain far below the target. |
| NPC and police AI | 0.5/10 | Basic pursuit, panic and visibility checks work, with no advanced tactical coordination or nuanced civilian behavior. |
| Missions and story | 0.5/10 | Six original sequential contracts with conversations, failure/retry and saves are verified, but campaign scale and dramatic presentation remain small. |
| Audio | 0.5/10 | Original radio, engines and ambience pass synthesis checks; richer movement audio is queued and native listening or speech is still unverified. |
| Performance and stability | 0.5/10 | Bounded workers and native fence/epoch tests pass, but the software-rendering timing increase needs investigation and hardware performance remains unknown. |


## Milestone 05 — shared atmosphere and movement sound, 2026-10-02

Validated source: `9628aab94861bcba7d78e91c5717ca0cfaf3b351`.
[Windows build and launch evidence](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36948177017).
Release is 824,832 bytes; SHA256:
`6526232eed390ee5cf33341656ba09f40b99b81a4981a96714e577b2bbf8da4d`.
The verified single EXE and checksum are in `/workspace/outputs/`. Evidence,
original captures, import audits and reports are under `validation/milestone-05/`.

Fully fogged geometry now evaluates the same procedural sky, clouds and weather
as the background. Inspected coast and aircraft captures show matching atmosphere;
short terrain coverage is still evident and is being expanded in the next batch.
Closer car and boat inspection angles show Leena and her carrier correctly.
Five authored footstep materials, swimming strokes and lateral tire friction
respond to real contact and movement. Pause, teleport, entering/exiting vehicles
and airborne motion are gated, with deterministic synthesis and state tests.

Both native configurations pass world/worker, 29 gameplay, audio and cinematic
suites with no compiler warnings or errors. 26 isolated executable launches
rendered 992 frames, including 19 Release scenes, both streaming routes and actual
Debug 4x/1x/2x resize/fullscreen checks, without D3D12 corruption/error messages.
Release WARP city/streaming processes took 30.331/29.359 seconds; Debug lifecycle
runs took 35.894/31.262/33.736 seconds. These are uncontrolled process timings,
not hardware frame-time evidence. Same-runner comparison is underway. Native
listening, hardware DXR, clean Windows 10, physical controller and 1440p/60 remain
unverified.

### Milestone 05 scorecard

| Category | Score | Evidence against GTA 6 |
| --- | ---: | --- |
| Map scale and variety | 1/10 | Several procedural regions stream correctly, but unique location density and visible coverage remain far below the benchmark. |
| Visual fidelity | 0.5/10 | Inspected atmosphere blends consistently and passenger views work, while geometry, characters, animation and lighting remain rudimentary. |
| World density and life | 0.5/10 | Basic traffic and pedestrians populate streets without rich routines, crowds or incidental interactions. |
| Vehicles and driving | 1/10 | Four vehicle classes and tested campaign routes work, with limited simulation, damage and presentation depth. |
| On-foot and combat | 0.5/10 | Contact-sensitive movement sound improves feedback, but animation, weapon variety, encounters and interaction remain limited. |
| NPC and police AI | 0.5/10 | Visibility-aware pursuit and panic exist without complex tactics or nuanced civilian behavior. |
| Missions and story | 0.5/10 | Six original contracts and short conversations work, but story scale, acting and cinematic detail remain small. |
| Audio | 0.5/10 | Original footsteps, splashes and tire scrub join radio and engines; speech, acoustics and native listening still lag or remain unverified. |
| Performance and stability | 0.5/10 | Both native builds pass isolated launches and Debug resource checks; software-rendering cost is under investigation and target GPU performance is unknown. |


## Milestone 06 — distant world coverage and visibility, 2026-10-02

Validated source: `3c25f7def4417a6ad22d95ca3b695ab01f4a710d`.
[Windows build and launch evidence](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36949428131).
Release is 909,824 bytes; SHA256:
`61673c04ced42197e3aadb22251d6bf92b522000276a66be66ef7f4ddcd60e7d`.
The verified EXE and checksum are in `/workspace/outputs/`. Its seven imports are
Windows system libraries; shaders remain embedded and the C/C++ runtime static.

A separate medium/far visual cache extends coverage while keeping 49 detailed
collision tiles. Two bounded workers generate missing coverage first. One
representation is selected per cell, with a short medium-detail hysteresis ring;
visibility-aware rendering retains unchanged GPU geometry and eligible ray data.
Main-scene reverse depth, conservative frustum tests, ground-anchored aircraft
shadows and coverage-driven fog support the longer view. Causeway terrain and
coarse road clearance are corrected. Inspected aircraft, survey, coast and night
captures show continuous distant geography and coherent atmospheric blending;
repetition and simple geometry remain conspicuous.

Both native builds pass all build/test targets, city and legacy streaming
checks, the five-phase distant route, and Debug 4x/1x/2x resource lifecycle
checks. The route tests cold coverage, adjacent movement, inland relocation,
coastal relocation and epoch reset. Release phases settle at frames
44/48/88/133/137, with 2,048–2,052 m ready coverage and 2,000 m fog endpoints;
settled upload batches, retired bytes and ordinary/pressure/repack waits are
zero. The 19-scene Release sweep also passes. Across the run, 28 executable
launches render 1,632 frames. Native software rendering verifies these paths,
not hardware DXR or target frame rate.

Release city/legacy-streaming/LOD processes take 34.134/29.164/52.935 seconds.
These include setup and capture and are not per-frame GPU measurements. The
separate controlled comparison of old combined and per-chunk rendering found
only a 0.755% difference in 120-frame medians, below observed run variation,
with identical images. A nonblocking timestamp candidate is next. Hardware
DXR, 1440p/60 on an RTX 4070, native listening, physical controllers, clean
Windows 10 and long native soaks remain unverified.

### Milestone 06 scorecard

| Category | Score | Evidence against GTA 6 |
| --- | ---: | --- |
| Map scale and variety | 1/10 | A large procedural geography now has tested distant coverage, but individual neighborhoods and activity density lack the benchmark's authored variety. |
| Visual fidelity | 0.5/10 | Two-kilometer views and coherent fog improve continuity, while repetitive buildings, simple vegetation, characters and lighting remain far below the target. |
| World density and life | 0.5/10 | Traffic and pedestrians populate the streets without rich daily routines, crowd diversity or incidental interactions. |
| Vehicles and driving | 1/10 | Four controllable vehicle classes and complete campaign routes work, with limited handling, damage and animation depth. |
| On-foot and combat | 0.5/10 | Movement, aiming, shooting and contact audio function, but encounters, animation and interaction breadth remain sparse. |
| NPC and police AI | 0.5/10 | Basic pursuit and civilian panic are tested without advanced tactics, social behavior or persistent routines. |
| Missions and story | 0.5/10 | Six original contracts have objectives, conversations and saves, while narrative scale and presentation remain small. |
| Audio | 0.5/10 | Original synthesized radio, ambience, vehicles and movement feedback work; voiced acting, acoustics and native listening remain absent or unverified. |
| Performance and stability | 0.5/10 | Bounded streaming and 28 native launches pass, but software-rendering checks do not establish hardware performance or extended crash-free operation. |
