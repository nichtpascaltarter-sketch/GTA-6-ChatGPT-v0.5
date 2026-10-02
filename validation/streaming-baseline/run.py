#!/usr/bin/env python3
"""Rebuild a committed World snapshot and record bounded CPU streaming routes."""
import argparse
import csv
import io
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tarfile
import tempfile
import time

HERE = Path(__file__).resolve().parent
DEFAULT_REVISION = "88a2cf6e24f3c7037581142915f757d8a49fa5e0"
FLAGS = ["-std=c++20", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic"]
ROUTES = ("city_axial", "city_diagonal", "coastal_aircraft")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default=DEFAULT_REVISION)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    if not hasattr(os, "sched_getaffinity") or not Path("/proc/self/status").exists():
        parser.error("this CPU/memory benchmark requires Linux affinity and /proc")
    root = Path(subprocess.check_output(
        ["git", "-C", str(HERE), "rev-parse", "--show-toplevel"], text=True).strip())
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", args.revision + "^{commit}"], text=True).strip()
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="meridian-stream-run-"))
    if output == HERE:
        parser.error("choose a separate output directory to preserve archived evidence")
    output.mkdir(parents=True, exist_ok=True)
    cpu = min(os.sched_getaffinity(0))
    model = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                  if line.startswith("model name")), "unknown")
    environment = {
        "revision": revision, "platform": platform.platform(), "cpu_model": model,
        "compiler": subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0],
        "flags": " ".join(FLAGS), "affinity_available": sorted(os.sched_getaffinity(0)), "bound_cpu": cpu,
        "timing": "steady_clock wall time of synchronous CPU functions; no renderer/GPU or sanitizer",
        "memory": "/proc/self/status sampled while combinedMesh lives; capacities exclude allocator metadata",
    }
    (output / "environment.json").write_text(json.dumps(environment, indent=2) + "\n")
    summary = {}
    with tempfile.TemporaryDirectory(prefix="meridian-stream-build-") as temporary:
        build = Path(temporary)
        archive = subprocess.check_output(["git", "-C", str(root), "archive", revision,
                                           "src/world.cpp", "src/world.h", "src/mc_math.h"])
        with tarfile.open(fileobj=io.BytesIO(archive)) as source:
            for name in ("src/world.cpp", "src/world.h", "src/mc_math.h"):
                destination = build / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(source.extractfile(name).read())
        executable = build / "stream_benchmark"
        subprocess.run([args.compiler, *FLAGS, str(HERE / "stream_benchmark.cpp"),
                        str(build / "src/world.cpp"), "-I", str(build), "-o", str(executable)], check=True)
        for route in ROUTES:
            started = time.perf_counter()
            with (output / (route + ".csv")).open("w") as data, (output / (route + ".log")).open("w") as log:
                child = subprocess.Popen([str(executable), route], stdout=data, stderr=log,
                                         preexec_fn=lambda: os.sched_setaffinity(0, {cpu}))
                try:
                    code = child.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
                    raise
            if code:
                raise RuntimeError(f"{route} exited with {code}; inspect {output}")
            with (output / (route + ".csv")).open() as data:
                rows = list(csv.DictReader(data))
            boundary = [row for row in rows if row["kind"] == "boundary"]

            def metrics(key):
                values = sorted(float(row[key]) for row in boundary)
                return {"median": statistics.median(values),
                        "p95": values[math.ceil(len(values) * .95) - 1], "max": max(values)}

            record = {"exit": code, "wall_seconds": time.perf_counter() - started,
                      "boundary_samples": len(boundary), "cold": rows[0],
                      "stream_ms": metrics("stream_ms"), "combined_ms": metrics("combined_ms"),
                      "total_ms": metrics("total_ms"),
                      "new_tiles": {key: sum(row["new_tiles"] == key for row in boundary)
                                    for key in sorted({row["new_tiles"] for row in boundary}, key=int)}}
            for name, column in (("max_vertices", "vertices"), ("max_indices", "indices"),
                                 ("max_chunk_used_bytes", "chunk_used_bytes"),
                                 ("max_chunk_capacity_bytes", "chunk_capacity_bytes"),
                                 ("max_combined_bytes", "combined_bytes"), ("peak_rss_kib", "hwm_kib")):
                record[name] = max(int(row[column]) for row in rows)
            record["max_cpu_mesh_capacity_bytes"] = max(
                int(row["chunk_capacity_bytes"]) + int(row["combined_bytes"]) for row in rows)
            summary[route] = record
            (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
            print(f"{route}: {record['boundary_samples']} boundary events; total ms {record['total_ms']}", flush=True)
    print(f"Results: {output}")


if __name__ == "__main__":
    main()
