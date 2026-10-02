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

- Source repository: `nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5`.
- Working branch: `development/meridian-coast`, based on initial commit `76bbb11`.
- Execution environment: Linux, `g++` available, no local Windows SDK, DXC,
  Windows runtime, or GPU validation. The connected Windows desktop is offline.
- Native Windows Release/Debug build and WARP screenshot validation passed in
  GitHub Actions at `1bbac2e` and `fd058a8`. Release also passed at `b1a34b7`;
  its Debug lifecycle run is pending. A WARP result is software-rendering evidence and
  cannot establish RTX 4070 performance or DXR correctness.
- There is no claim of a complete game, AAA visual quality, crash freedom,
  benchmark parity, or measured 1440p/60 performance.

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

- World tests passed optimized C++20 with warnings treated as errors. At
  `b1a34b7`, the origin contains 335,720 triangles, 700,724 vertices, 49 chunks.
  Tests include seams, deterministic generation, chunk reuse,
  biomes, mesh validity, high-speed collision, sliding, and overlap recovery.
- Audio synthesis tests passed optimized warnings-as-errors and ASan/UBSan.
- Windows Release/Debug executable builds, launch, screenshot inspection, and
  debug-layer validation passed at `1bbac2e`. Native audio playback, hardware
  DXR, and hardware frame-time measurements remain unverified.

## Active tasks

Latest combined content checkpoint: `c2080088c3e681aab6b784075d21a4793be9f1ac`,
[native run](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36946139617).
Release passed all native tests, 120 frames, and 13 scene captures. Debug passed
the base launch and stricter lifecycle checks. Native images confirm boats,
aircraft, new vegetation, map UI, removal of facade banding, and preserved night
light pools. The city WARP launch step returned to 15 seconds after negligible
dusk-light pruning (previously 70 seconds, baseline 14 seconds).

Final repairs awaiting another native pass: `f61abc7` keeps abandoned planes
under physics and uses full 3D police sight distance; `6513081` tests those cases
(19 gameplay suites pass, and archived pre-fix code fails the three regressions).
`39214d4` draws the actual winding coastal road on the map, reports horizontal
waypoint distance, and keeps bitmap glyph cells at least one physical pixel.
The latter fixes disappearing glyph rows in the 960x541 map/craft HUD captures.
Coastal route samples and the existing world suite pass strict C++20 checks.

Playable craft now include buoyancy, rudder response, swimming/reboarding,
aircraft throttle/banking/lift/stall/landing, a 512 m airstrip, and a walkable
coastal pier. Save version 2 preserves craft attitude and throttle and migrates
version 1. Audio adds four distinct engines and continuous biome ambience;
signal tests and ASan/UBSan pass, but native listening remains unverified.
The new world map supports pan, zoom, waypoints, and craft markers. Root owns
map/UI/main, gameplay owns game/visuals/tests, renderer owns renderer/shaders,
world owns generation, and build validation owns scripts/CI.

After the final repair checkpoint: append Milestone 02 with native evidence and
scorecard, then begin linear-HDR offscreen rendering, MSAA edge antialiasing and
a dedicated post-process pass. Keep UI crisp. Follow with incremental chunk
GPU residency/BLAS and asynchronous generation; a detailed architecture review
is available in the current conversation. Keep the full original target.

Current lighting candidate: `b1a34b7037069fb04fd58062d76e6ed86f602347`, undergoing
[native validation](https://github.com/nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5/actions/runs/36945666811).
Release passed all four native test executables, an isolated-EXE 120-frame WARP
launch, and ten additional camera/weather captures. Debug passed compilation,
tests, and the base launch; lifecycle validation remains pending. The previous
candidate `fd058a8` passed both native configurations and an eight-scene sweep.

Implemented since Milestone 01: directional raster shadows, six building
families, a market/clock pavilion, rounded actor/vehicle meshes, four original
subtitled cinematic conversations, street/shop point lights, vehicle spotlights,
police flashers, and muzzle illumination. Conversations are not voiced; local
lights are unshadowed. Review fixed close-wall cinematic cameras, menu/skip
input overlap, and continuity of shader animation time.

Inspected `b1a34b7` captures: night streets and headlights now read clearly;
portraits remain very primitive, and vertical facade bands still need repair.
Release's WARP base smoke step increased from 14 seconds at `fd058a8` to 70
seconds at `b1a34b7`; investigate the lighting/shadow cost before accepting the
milestone. This is CI software-rendering time, not hardware frame-rate evidence.

An independent ASan/UBSan soak on archived `fd058a8` passed 38,880 updates / 648
simulated seconds, 152 mesh validations, 52 byte-exact save/load round trips,
and 16 boundary/biome placements without sanitizer failures or leaks. Harness
and log: `/tmp/meridian-soak-tYMekg/`.

Current unvalidated content work: biome-specific vegetation, coastal dock and
inland airstrip, playable boats/aircraft with save migration, and world-map
navigation. Root's map projection/finite-geometry checks passed against an
isolated `b1a34b7` source snapshot; native map capture is pending. Lifecycle
checks now also verify actual Win32 rectangles/styles and client dimensions.

Captures: `/workspace/scratch/lighting-b1a34b7/` and
`/workspace/scratch/visual-milestone-fd058a8/`. The executable currently copied to
`/workspace/outputs/MeridianCoast.exe` is the verified `fd058a8` Release binary.

1. Repair facade banding and investigate WARP regression, finish Debug lifecycle
   and visual verification, then append the second evidence-backed scorecard.
2. Finish boats, aircraft, swimming, launch sites, vegetation, world map and
   contextual controls; run native craft/map/biome captures and physics tests.
3. Remove synchronous streaming stalls, add distant world representation, and
   include moving vehicles/characters in acceleration structures.
4. Expand authored story, cutscenes, side activities, civilian routines, crime
   witnesses, police tactics, driving dynamics, motorcycles, boats, and aircraft.
5. Add true dynamic GI, volumetric atmosphere, detailed authored procedural
   materials, richer animation, interiors, crowds, and robust destruction.
6. Measure real Windows hardware performance and reliability; optimize against
   frame-time distributions at 2560x1440 on RTX 4070-class hardware.

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
