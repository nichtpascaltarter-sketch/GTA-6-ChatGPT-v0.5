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
- Latest verified executable: `a56e0584d9fbb56aeb2dfe50e4eb92183b69737e`,
  copied to `/workspace/outputs/MeridianCoast.exe`; see Milestone 02 below.
- Native Release/Debug, system-import audits, 19 gameplay suites, 13 additional
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
- Native 19/19 gameplay suites pass, including actual runway takeoff, banking,
  stall descent, landing, boat handling/swimming, old-save migration, passive
  aircraft motion, and altitude-aware police visibility. Core craft tests passed
  ASan/UBSan. New aircraft exit preference tests bring the working tree to 21
  passing strict portable suites; those later changes await the next native run.
- Audio synthesis passes strict tests and ASan/UBSan, including four engine
  sounds, biome ambience, finite output, transition continuity and chunking.
- Archived `fd058a8` passed a 648-simulated-second sanitizer soak, 38,880 updates,
  152 mesh validations, 52 exact save/load round trips and 16 boundary cases.
  A new craft-focused 570-second soak of `a56e058` is running; result pending.
- Native audio listening, real controller hardware, clean Windows 10 coverage,
  hardware DXR, and 2560x1440 RTX 4070 frame-time measurements remain unverified.

## Active tasks

1. HDR/MSAA rendering is now in progress: linear floating-point scene color,
   queried 4x/2x MSAA with 1x fallback, resolve, tone mapping, restrained bloom,
   then crisp UI. Renderer agent owns renderer/shaders; coordinate build-time DXC
   additions with build validation. Validate resize resources and all captures.
2. Include the already committed aircraft exit preference improvement (`b8c4821`,
   tests `0f1770e`) and smoke process-duration reporting (`798a0f8`) in the next
   native checkpoint. The original deep-water exit was already safe; the actual
   improvement prefers clear dry land/deck and reports swimming correctly.
3. Complete the craft sanitizer soak and preserve results. Root owns main/UI/map
   and milestone documentation; gameplay owns game/visuals/tests; world owns
   deterministic generation; build validation owns build scripts and CI.
4. Remove full-world GPU replacement and synchronous generation stalls. Preserve
   per-chunk GPU geometry and BLAS, retire allocations by fence, add per-instance
   reflection index metadata, then use bounded application-owned workers. Keep
   load/reset epochs separate from stable per-key job tickets.
5. Add coarse world coverage to 1.5–2 km using shared building descriptors,
   8 m boundary samples with 16/32 m interiors, and bounded mesh capacities.
   Keep one representation per tile and coordinate fog/far-plane changes. See
   agent architecture reports in the current conversation for full design.
6. Expand story, side activities, pedestrian routines, police tactics, interiors,
   destruction, animation, true dynamic GI and volumetric atmosphere. Measure
   actual hardware frame times and compatibility as access becomes available.

Captures for the verified checkpoint are in
`/workspace/scratch/repair-a56e058/`; Debug evidence is in
`/workspace/scratch/debug-a56e058/`. Machine-readable committed evidence is under
`validation/milestone-02/`. The full original target remains unchanged.

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
