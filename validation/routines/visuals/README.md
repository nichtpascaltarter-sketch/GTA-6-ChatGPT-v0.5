# Resident pose evidence

This is the compact archive of the renderer owner's resident pose validation, copied from `/workspace/scratch/routine-visuals-paired/compact-evidence`. It contains only authored probes, commands, hashes and text results. No executable or copied game-source tree is included.

The final pose source is commit `50d0d8b` (stock-check gaze), following `f578f39` (routine poses) and `c614d20` (manifest/pencil interaction). Its exact `src/visuals.cpp` and `tests/pedestrian_visuals_tests.cpp` SHA-256 values in `raw/final-source.sha256` still match the native resident candidate `c90828a`. Later gameplay/world changes did not alter these two files. The final sanitizer run also included the split bench collision geometry from `161d086`.

The final strict and ASan/UBSan logs verify stationary legs, seated pelvis and shoes, actual clipboard/pencil and gaze contact, parcel/carry/panic poses, all eight authored bench seats at both detail levels, and unchanged non-Work activity fingerprints. The nine common baseline scene fingerprints include walking crowds, the player, workshop attendant, trial marshal, and four rescue passenger/vehicle kinds.

The historical A/B/B/A comparison measured only Linux `Game::dynamicMesh()` generation, using immutable inputs and CPU affinity while other compilation was paused. Walking geometry hashes matched exactly. Close-crowd medians were 5.754393 ms baseline / 5.757325 ms candidate; distant-crowd medians were 1.817167 ms / 1.828391 ms. Those small differences do not establish a meaningful performance change. This is not Windows frame timing or GPU performance evidence.

The final mixed 100-person geometry probe reports 196,659 vertices, 192,137 triangles and 10,172,004 bytes. Its timing is descriptive only because the later Work refinement was not measured in the controlled idle window.

`raw/README.md` preserves the original chronological notes and exact historical invocations. Its scratch snapshot and executable paths describe the original run; those full snapshots are intentionally absent from this archive. `raw/manifest.sha256` also records omitted historical executable hashes and is not a manifest of archive membership. All included raw files can be checked with:

```sh
cd validation/routines/visuals/raw
sha256sum -c evidence.sha256
```

To rerun the final pose checks from repository source, use `validation/routines/visuals/run-final.sh`. It produces strict and sanitized binaries only under the ignored build directory. The baseline and intermediate measurements remain historical evidence; rerunning the current final suite is not a reconstruction of the earlier in-progress gameplay snapshots.

Native capture inspection is recorded separately once Windows artifacts are available. These portable geometric checks cannot establish final rasterized appearance, visual quality, or target hardware performance.
