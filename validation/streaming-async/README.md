# Bounded background world streaming: CPU measurements

World and worker snapshot: `9a621a7ebf17b652daee5f0de14075d14c694702`.

The asynchronous service retained a complete 49-chunk world and valid 3×3 collision coverage through all 702 boundary transitions in each affinity mode. Each mode installed exactly 5,490 requested chunks, with no synchronous fallback, rejection, missing publication, or queue-budget violation.

These are CPU-only measurements on the shared Linux Xeon Platinum 8573C host, built with g++ 14.2 and `-O2`. They do not establish Windows frame time, GPU upload performance, DXR cost, or the 1440p/60 target. `service_wall_ms` measures elapsed time inside all main-thread service calls required for one publication, including allocator and scheduler delays. It is not a thread CPU-time counter.

## Same-source synchronous comparison

The synchronous runner was rerun against the new World implementation on CPU 0. It generates the changed chunks and then calls `combinedMesh`, matching the [earlier baseline](../streaming-baseline/README.md). Cold loads are excluded.

| Route | Boundaries | Changed tiles | Generation median/p95/max ms | Combine median/p95/max ms | Total median/p95/max ms |
|---|---:|---:|---|---|---|
| Axial city | 96 | 7 | 3.747 / 4.337 / 5.611 | 3.711 / 10.140 / 12.258 | 7.457 / 14.370 / 17.007 |
| Diagonal city | 96 | 13 | 6.983 / 7.837 / 9.677 | 3.725 / 5.332 / 11.702 | 10.735 / 12.880 / 20.073 |
| Coastal aircraft | 510 | 7 | 4.261 / 4.841 / 6.427 | 1.766 / 2.074 / 5.161 | 6.019 / 6.823 / 9.232 |

## Two workers, three available cores

The process and its two worker threads share affinity CPUs 0, 1, and 2. No thread is pinned individually. Every simulated frame calls the service before and after movement; subsequent polls repeat at 60 Hz until the complete world publishes. The harness performs no game simulation or rendering between these calls.

| Route | Summed service time median/p95/max ms | Worst frame's two calls median/p95/max ms | Publication latency median/p95/max ms | Poll pairs median/max |
|---|---|---|---|---|
| Axial city | 0.082 / 2.575 / 5.655 | 0.040 / 2.532 / 4.954 | 33.470 / 33.794 / 34.438 | 3 / 3 |
| Diagonal city | 1.622 / 2.640 / 4.258 | 1.529 / 2.512 / 2.581 | 66.809 / 67.115 / 69.691 | 5 / 5 |
| Coastal aircraft | 0.078 / 3.160 / 5.928 | 0.039 / 2.790 / 5.914 | 33.457 / 33.647 / 35.703 | 3 / 3 |

The main thread does substantially less typical work, but tail callback times remain several milliseconds on this host. Publication latency is longer than generation time because the completed queue holds four results and the harness drains it at frame boundaries. Old detail and collision remain resident throughout that delay. The combined 49-chunk CPU mesh is absent from this path; renderer upload measurements are a separate validation.

## One-core contention control

With all three threads constrained to CPU 0, summed service times rise to 3.968 / 5.322 / 3.117 ms median for axial / diagonal / coastal routes. Corresponding p95 values are 4.826 / 7.442 / 3.701 ms. Worker scheduling can preempt the main thread inside a timed callback. This control demonstrates that moving generation to workers does not remove its CPU cost. Publication medians were 17.384 / 33.467 / 17.114 ms; those shorter delays reflect workers running during the contended callbacks, not greater total throughput.

## Bounds and memory

All runs stayed at or below eight queued requests, two active workers, and four completed results. The highest observed completed payload was 3,719,576 bytes, below the 32 MiB cap. The highest observed staged payload was 11,155,656 bytes. Payload accounting uses vertex/index/collision/light vector capacities, including unused reserved storage. Worker fault tests separately exercise the 8 MiB single-result rejection path and blocked producer cancellation.

| Route | Synchronous peak RSS MiB | Three-core async peak RSS MiB | Resident chunk capacity MiB | Old resident + combined capacity MiB |
|---|---:|---:|---:|---:|
| Axial city | 102.15 | 63.81 | 43.44 | 75.05 |
| Diagonal city | 105.83 | 65.36 | 43.44 | 75.05 |
| Coastal aircraft | 57.47 | 48.86 | 31.71 | 48.08 |

RSS includes allocator retention and thread stacks. Resident-capacity figures exclude the separately bounded completed and staged payloads. The service may also hold one locally built result per active worker and a batch being installed on the main thread; the completed queue cap is not a claim that all streaming memory is only 32 MiB.

## Route and measurement details

- Axial city: centers x = −8 through 8, z = 0, forward and reverse, three round trips.
- Diagonal city: centers (−8, −8) through (8, 8), forward and reverse, three round trips.
- Coastal aircraft: 5,001 samples from `World::coastalRoadPoint`, at terrain height + 160 m; retain successive changed chunk centers, then three forward/reverse passes. This evaluates the streaming footprint, not aircraft physics.
- Every initial load uses synchronous `World::stream` and is recorded separately in stderr. The async measurements do not hide an initial full-world generation inside a boundary sample.
- Each async publication verifies actual 3×3 collision coverage and 49 live tiles before/after polling. Published revision and service counters must show exactly one publication and zero fallback for every event.
- All measurements use `steady_clock`. p95 is nearest rank. Per-boundary CSVs, stderr counters, environment, and machine-readable summaries are included.
- No claim of identical scheduling across affinity modes is made; OS contention and allocator behavior affect the measurements.

## Reproduce

The driver reads the five world/worker source files directly from the recorded Git commit, compiles both harnesses, and writes fresh results outside this evidence directory:

```sh
python3 validation/streaming-async/run.py
```

Optional arguments include `--revision`, `--output`, `--compiler`, `--mode async_three`, and `--route city_axial`. The driver requires Linux `/proc` and CPU-affinity support; the game itself does not depend on this diagnostic script or Python.
