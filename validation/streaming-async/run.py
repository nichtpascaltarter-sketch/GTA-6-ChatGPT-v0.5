#!/usr/bin/env python3
"""Compare synchronous and bounded asynchronous streaming on fixed CPU routes."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
REVISION = '9a621a7ebf17b652daee5f0de14075d14c694702'
FLAGS = ['-std=c++20', '-O2', '-DNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-pthread']
ROUTES = ('city_axial', 'city_diagonal', 'coastal_aircraft')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', default=REVISION)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--mode', choices=('all', 'sync', 'async_single', 'async_three'), default='all')
    parser.add_argument('--route', choices=('all', *ROUTES), default='all')
    args = parser.parse_args()
    if not hasattr(os, 'sched_getaffinity') or not Path('/proc/self/status').exists():
        parser.error('this CPU/memory benchmark requires Linux affinity and /proc')
    root = Path(subprocess.check_output(['git', '-C', str(HERE), 'rev-parse', '--show-toplevel'], text=True).strip())
    revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', args.revision + '^{commit}'], text=True).strip()
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='meridian-async-run-'))
    if output == HERE:
        parser.error('choose a separate output directory to preserve archived evidence')
    output.mkdir(parents=True, exist_ok=True)
    cpus = sorted(os.sched_getaffinity(0))
    modes = {'sync': cpus[:1], 'async_single': cpus[:1], 'async_three': cpus[:3]}
    if args.mode != 'all':
        modes = {args.mode: modes[args.mode]}
    routes = ROUTES if args.route == 'all' else (args.route,)
    environment = {
        'revision': revision, 'platform': platform.platform(),
        'compiler': subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
        'available_cpus': cpus, 'flags': ' '.join(FLAGS), 'interval_ns': 16666667,
        'workers': 2, 'modes': modes,
        'timing': 'steady_clock elapsed main-thread callback time, including scheduling; no renderer or GPU',
    }
    (output / 'environment.json').write_text(json.dumps(environment, indent=2) + '\n')
    summary = {}
    with tempfile.TemporaryDirectory(prefix='meridian-async-build-') as temporary:
        build = Path(temporary)
        (build / 'src').mkdir()
        for name in ('world.cpp', 'world.h', 'mc_math.h', 'world_streamer.cpp', 'world_streamer.h'):
            (build / 'src' / name).write_bytes(subprocess.check_output(['git', '-C', str(root), 'show', revision + ':src/' + name]))
        for kind in ('sync', 'async'):
            command = [args.compiler, *FLAGS, str(HERE / (kind + '_benchmark.cpp')), str(build / 'src/world.cpp')]
            if kind == 'async':
                command.append(str(build / 'src/world_streamer.cpp'))
            subprocess.run([*command, '-I', str(build), '-o', str(build / (kind + '_benchmark'))], check=True)
        for mode, affinity in modes.items():
            for route in routes:
                label = mode + '_' + route
                started = time.perf_counter()
                with (output / (label + '.csv')).open('w') as data, (output / (label + '.stderr.txt')).open('w') as log:
                    executable = build / ('sync_benchmark' if mode == 'sync' else 'async_benchmark')
                    child = subprocess.Popen([str(executable), route], stdout=data, stderr=log,
                                             preexec_fn=lambda: os.sched_setaffinity(0, set(affinity)))
                    try:
                        code = child.wait(timeout=180)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
                        raise
                if code:
                    raise RuntimeError(f'{label} exited with {code}; inspect {output}')
                with (output / (label + '.csv')).open() as data:
                    rows = list(csv.DictReader(data))
                boundary = [row for row in rows if row.get('kind', 'boundary') == 'boundary']

                def metrics(key):
                    values = sorted(float(row[key]) for row in boundary)
                    return {'median': statistics.median(values),
                            'p95': values[math.ceil(len(values) * .95) - 1], 'max': max(values)}

                record = {'exit': code, 'wall_seconds': time.perf_counter() - started, 'samples': len(boundary),
                          'new_tiles': {key: sum(row['new_tiles'] == key for row in boundary)
                                        for key in sorted({row['new_tiles'] for row in boundary}, key=int)},
                          'peak_rss_kib': max(int(row['hwm_kib']) for row in rows)}
                if mode == 'sync':
                    record.update({key: metrics(key) for key in ('stream_ms', 'combined_ms', 'total_ms')})
                    record['cold'] = rows[0]
                    record['max_capacity_bytes'] = max(int(row['chunk_capacity_bytes']) + int(row['combined_bytes']) for row in rows)
                else:
                    record.update({key: metrics(key) for key in ('service_wall_ms', 'max_pair_ms', 'max_call_ms', 'publish_latency_ms', 'poll_pairs')})
                    for key in ('resident_capacity_bytes', 'new_capacity_bytes', 'peak_completed_bytes', 'peak_staged_bytes',
                                'peak_queued', 'peak_inflight', 'peak_completed'):
                        record[key] = max(int(row[key]) for row in rows)
                    for key in ('fallbacks', 'publications', 'scheduled', 'rejected'):
                        record[key] = sum(int(row[key]) for row in rows)
                summary[label] = record
                (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
                print(label, json.dumps(record), flush=True)
    print('Results:', output)


if __name__ == '__main__':
    main()
