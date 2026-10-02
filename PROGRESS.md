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
  GitHub Actions at `1bbac2e`. A WARP result is software-rendering evidence and
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

- World tests passed optimized C++17 with warnings treated as errors. Origin:
  199,220 triangles, 411,640 vertices, 49 chunks, approximately 32 ms generation
  on this Linux host. Tests include seams, deterministic generation, chunk reuse,
  biomes, mesh validity, high-speed collision, sliding, and overlap recovery.
- Audio synthesis tests passed optimized warnings-as-errors and ASan/UBSan.
- Windows Release/Debug executable builds, launch, screenshot inspection, and
  debug-layer validation passed at `1bbac2e`. Native audio playback, hardware
  DXR, and hardware frame-time measurements remain unverified.

## Active tasks

1. Validate the new scenery/weather capture sweep and window lifecycle changes.
2. Add raster shadowing and improve scene composition, close-up characters, and
   vehicle shapes using captured output for each change.
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
