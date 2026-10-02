# Resident and market capture compatibility

Both existing probes pass against combined market/police source
`e048adf3f1d41394b5f9a08048cc1902b411f379`. All eight Game/world compilation units
were rebuilt into a fresh scratch object set using strict GCC warnings as
errors. Assertions remained enabled. The 19-file dependency manifests match
before and after execution. No production source, test, or capture helper was
changed for this check.

## Natural residents

`validation/routines/native-capture-probe.cpp` prepares all six views through
the current `ResidentCapture` helper and passes its unknown-name and immediate
cancellation checks. Each result retains 84 people and reports 16 persistent
residents.

| Scene | Actual activity | Warm-up, simulated seconds |
| --- | --- | ---: |
| residents-carry | Carrying | 1.167 |
| residents-work | Checking stock | 30.033 |
| residents-bench | Seated | 74.467 |
| residents-talk | Talking | 117.433 |
| residents-startle | Startled | 117.567 |
| residents-flee | Fleeing | 118.367 |

The helper advances the initialized simulation at 30 Hz. It requires stable
activities, actual movement when carrying/fleeing, a nearly completed seating
blend, and a nearby live conversational partner. The alarm cases emit an actual
player gunshot and require the same previously calm resident to react. It does
not assign civilian positions, activities, routes, or poses. Selected head,
torso, and applicable prop sightlines clear collision solids, static triangles,
and dynamic triangles; the dynamic test ignores the first 0.4 m around the
subject. Full state and camera values are in `resident.log`.

## Market views

`validation/tide-hall-market/integration/view_probe.cpp` passes all five fixed
production views: day, night, citrus, tea, and bread. All 28 authored surface
samples fit the specified 68-degree, 16:9 camera frame and clear static and
dynamic triangle sightlines. The night view selects four market lights; the
four daytime views select none. The probe verifies unchanged pedestrian and
vehicle counts, positions, and identities, clear spectator/camera positions,
exact requested time/weather, and rejection of an unknown scene. Results are
in `market.log`.

These are portable simulation and camera-preparation checks. They do not
establish D3D12 pixel output, complete-frame visibility, audio output, or
performance. The market views do not advance the simulation. Native acceptance
of the combined executable is recorded separately.
