# Distant-world warmup and mesh arrival measurements

Source snapshot: `ee19f361b3e1c73de1a325ef9bf99f039d45f7f0`. The archived seven source files used by the original runs were checked byte-for-byte against that commit. These measurements cover the CPU streaming service, not a Windows render frame or GPU upload fence.

The default distant completion quota is **16**, with two workers, eight queued requests, a 32 MiB completed-payload cap, and 256 KiB maximum per coarse chunk. Detailed-only mode retains its existing quota of four. The service drains a bounded stack batch; unchanged idle calls allocate no completion batch and do not wake workers.

## Results

All runs start at `(8, 0, 8)` with the ordinary 49 detailed chunks loaded synchronously and no distant geometry cached. That initial detailed load is excluded from the timings below. The timed work ends only when all 1,450 distant tiles are resident and all jobs have settled. These are additional LOD startup costs, not whole-application launch times.

| Mode | Completion quota | Time to continuous 2 km | Full cache | Callback pair median / p95 / max | Largest newly selected mesh payload per pair |
|---|---:|---:|---:|---|---:|
| Continuous update/yield | 16 | 253.725 ms | 401.278 ms | 1.184 / 1.483 / 1.707 ms | 377,784 B |
| 60 Hz polling | 4 | 3,686.017 ms | 4,984.480 ms | 0.620 / 2.501 / 2.812 ms | 517,152 B |
| 60 Hz polling | 16 | 1,984.508 ms | 3,034.392 ms | 0.911 / 1.027 / 1.176 ms | 517,152 B |
| 60 Hz polling | 32 | 1,967.921 ms | 3,001.348 ms | 0.950 / 1.422 / 4.753 ms | 515,504 B |

The sixteen-result quota substantially improves warmup over four. Raising it to 32 gives little additional benefit because the eight-request queue is refilled on main-thread updates. The largest measured selected payload with the default quota was about **0.493 MiB**, below the proposed 2 MiB per-frame upload budget. This is observed behavior of the origin route, not a new global upload guarantee.

After warmup, 10,000 unchanged pre/post callback pairs averaged about 0.6 microseconds per pair on this host. Timings use `steady_clock`, include scheduling effects, and were recorded on a shared Linux Xeon host with process affinity on CPUs 0, 1 and 2. No rendering or game simulation runs between the two service calls. Tail differences between runs should not be interpreted as precise platform guarantees.

## Correctness and memory

Every run settled with:

- 49 live detailed collision chunks and valid 3×3 collision coverage throughout.
- 1,450 cached coarse tiles: all 1,225 Far tiles through radius 17 plus all 225 Medium tiles through radius 7.
- Exactly 1,089 selected meshes: 49 Detail, 176 Medium, and 864 Far. Hidden coarse underlays and the outer prefetch band are not selected.
- A conservative coverage radius of **2,056 m**, measured from `(8, 8)` to the nearest missing tile or active-area boundary.
- 54,562,800 bytes of coarse vector capacities, with zero budget deferrals and zero synchronous collision fallbacks.
- 45,806,360 total bytes of newly selected vertex/index data over warmup. A Far mesh replaced by Medium counts as a new payload; cache-only underlays do not.

The geometry tests separately generate ten complete regional caches. The largest measured one contains 56,567,456 bytes (53.95 MiB), below the 64 MiB hard coarse-cache bound. Detailed fallback geometry has its own 64 MiB bound. Capacity accounting includes unused reserved vertex/index/collision/light storage; map nodes, allocator metadata, stacks and other process allocations are outside those payload counters.

The lifecycle tests exercise stale epochs, overlap tickets, exact selection, off-center coverage, cancellation, cache saturation and recovery, and emergency Far generation when detailed fallback storage fills. The saturated-cache handover regression demonstrably fails the old eviction rule, which could discard the only post-publication representation beneath departing detail.

## Geometry changes validated with this milestone

The direct emitters share resolved building styles, footprints and heights, named landmarks, transport sites, and natural-tree placements. Terrain uses 512 / 160 / 80 triangles per Detail / Medium / Far tile while preserving the same eight-metre perimeter samples and normals. Tests cover 270 exact mixed-LOD seam/normal pairings, all six city building styles, suburban roof features, and 374 vegetation anchors.

The causeway now follows the greater of its existing bridge profile and natural island ground. The deep-water section at x = 3,200 m remains 5.2 m high; roads, segmented rails, supports, and the causeway landmark follow the same profile as walking/driving. Terrain-clipped road surfaces also remove coarse-interpolation burial. Tests validate 19,800 road-clearance samples across 119 chunks, with a minimum recorded terrain clearance of 0.024999 m. Central detailed geometry remains 726,020 vertices and 342,248 triangles.

## Reproduce

```sh
python3 validation/lod-streaming/run.py
```

The script reads the seven source files from the recorded Git commit and writes fresh results to a separate temporary directory. Optional arguments include `--revision`, `--output`, `--compiler`, and `--mode yield-16` (or `paced-4`, `paced-16`, `paced-32`). It requires Linux CPU-affinity support. Python is only a development diagnostic dependency; it is not used by the game executable.
