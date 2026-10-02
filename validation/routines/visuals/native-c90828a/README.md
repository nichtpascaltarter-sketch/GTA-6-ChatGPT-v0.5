# Initial native resident inspection: c90828a

Inspected Release captures from native run `36952070205`, extracted by the build owner at `/workspace/scratch/residents-c90828a-release`. The accepted comparison captures are `/workspace/scratch/combined-b0c0c4c-release`. Image hashes are recorded here; no images or executables are copied into this archive.

This is an initial failed visual gate: **the Work capture is not accepted**. The worker and clipboard are completely hidden behind market stall/crate geometry across approximately x330–850, y145–435. The original camera probe checked collision solids and dynamic triangles, so it missed the noncolliding static decoration. The fix belongs in capture visibility against rendered static geometry, followed by a new native image inspection. These records must not be used to claim that the original Work view passed.

The other five resident captures are readable: Carry shows the parcel supported at waist height during an actual stride; Bench shows the pelvis on the seat, bent knees and grounded shoes; Talk shows two facing people with distinct gestures; Startle shows their raised hands after the shot; Flee shows running poses. Native source preparation already checks the actual motion/reaction history; a still image alone does not establish animation continuity.

The narrow dark foreground triangle in Talk/Startle is the authored fountain basin rim viewed almost edge-on, not a near-plane or malformed-triangle defect. `ray_probe.log` records exact camera rays against the generated static and dynamic meshes. In Talk, pixel(270,440) hits the rim side 3.89973 m away at the triangle `(52,.1,64) / (52.4089,.1,67.1058) / (52.4089,.8,67.1058)`. In Startle, nearby sampled rays hit the rim top/side at 2.8–4.2 m. The far-left blue polygon is the basin water. The cameras should remain unchanged for this repair; only Work requires corrected subject visibility.

Regression inspection against b0c0c4c:

- Boat and plane passenger BMPs are byte-identical.
- Car passenger rectangle `[445,208,525,302)` and motorcycle rider rectangle `[417,194,511,289)` are pixel-identical (7,520 and 8,930 pixels).
- Portrait appearance is unchanged. In rectangle `[430,212,532,430)`, 116 changed pixels are confined to y265–269 around the silhouette, matching the new background crosswalk. Torso rectangle `[440,310,523,407)` is unchanged. Background residents/crosswalks differ as expected.

The probe was compiled against the unchanged game source in the routines worktree (documentation head7182649; game source identical to c90828a). Reproduce from the repository root:

```sh
mkdir -p build/resident-visual-validation
c++ -std=c++20 -O2 -pthread -Isrc validation/routines/visuals/native-c90828a/ray_probe.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp -o build/resident-visual-validation/ray_probe
build/resident-visual-validation/ray_probe
```

This probe reproduces the original capture helper. After the helper changes, the source manifest identifies the historical inputs needed to reproduce this initial result.
