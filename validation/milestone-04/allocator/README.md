# GPU arena allocation diagnostics

Run from the repository root:

```sh
bash validation/milestone-04/allocator/run.sh
```

The script extracts `ArenaRange` and `ArenaAllocator` directly from the current
`src/renderer.cpp`, compiles the two probes in a temporary directory, refreshes
the three logs here, and removes all generated binaries and extracted code.
`CXX` may select another C++20 compiler. The shell script, Python standard library,
and compiler are diagnostic tools only; they are not game dependencies.

- `randomized.cpp` performs 200,000 deterministic allocation/release operations
  (40 trials of 5,000 operations, seed 752994). Every step verifies live/free
  non-overlap, bounds, complete capacity accounting, sorted/coalesced free
  ranges, and eventual recovery of the full arena. The invocation enables
  AddressSanitizer and UndefinedBehaviorSanitizer.
- `route.cpp` generates the actual world meshes at the eight native probe
  positions. It allocates incoming chunks before releasing outgoing chunks,
  using the real allocator with 64 MiB of vertex capacity and 16 MiB of index
  capacity. The last phase invalidates all chunks to model a new epoch. The log
  records the expected added/retained counts without a pressure wait or repack.
  Each phase assumes the preceding phase has retired, matching the native
  readiness-driven probe.

These portable tests validate allocation behavior and capacity for this route.
They do not execute D3D12, prove asynchronous resource lifetimes, or validate DXR
hit decoding. Those require native debug-layer and hardware runs respectively.
`invocations.log` identifies the tested revision, relevant source checksums,
compiler, and exact compile/run invocations. Temporary paths in that log refer
to the disposable directory that the script removes on exit.
