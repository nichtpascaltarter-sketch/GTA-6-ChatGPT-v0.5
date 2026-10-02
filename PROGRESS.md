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
- Native Windows Release/Debug build and WARP screenshot validation are being
  prepared in GitHub Actions. A WARP result is software-rendering evidence and
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
- Windows executable build, launch, screenshot inspection, native audio, hardware
  DXR, and hardware frame-time measurements are pending.

## Active tasks

1. Integrate gameplay and validate complete mission/save loops.
2. Build both Windows configurations, fix all compilation failures, launch WARP,
   inspect captured frames, and repair visual defects.
3. Record exact commits, build evidence, and the first milestone scorecard.
4. Add raster shadowing and improve scene composition, close-up characters, and
   vehicle shapes using captured output for each change.
5. Remove synchronous streaming stalls, add distant world representation, and
   include moving vehicles/characters in acceleration structures.
6. Expand authored story, cutscenes, side activities, civilian routines, crime
   witnesses, police tactics, driving dynamics, motorcycles, boats, and aircraft.
7. Add true dynamic GI, volumetric atmosphere, detailed authored procedural
   materials, richer animation, interiors, crowds, and robust destruction.
8. Measure real Windows hardware performance and reliability; optimize against
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
