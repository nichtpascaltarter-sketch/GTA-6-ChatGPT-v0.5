#!/usr/bin/env python3
"""Build a committed World snapshot and measure distant warmup and selected mesh arrivals."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REVISION = 'ee19f361b3e1c73de1a325ef9bf99f039d45f7f0'
FLAGS = ['-std=c++20', '-O2', '-DNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-pthread']


def parse_output(text):
    result = {}
    for token in text.split():
        key, value = token.split('=', 1)
        if '/' in value:
            result[key] = [int(item) for item in value.split('/')]
        elif '.' in value:
            result[key] = float(value)
        else:
            result[key] = int(value)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', default=REVISION)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--mode', choices=('all', 'yield-16', 'paced-4', 'paced-16', 'paced-32'), default='all')
    args = parser.parse_args()
    if not hasattr(os, 'sched_getaffinity'):
        parser.error('this benchmark requires Linux CPU-affinity support')
    root = Path(subprocess.check_output(['git', '-C', str(HERE), 'rev-parse', '--show-toplevel'], text=True).strip())
    revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', args.revision + '^{commit}'], text=True).strip()
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='meridian-lod-run-'))
    if output == HERE:
        parser.error('choose a separate output directory to preserve archived evidence')
    output.mkdir(parents=True, exist_ok=True)
    cpus = sorted(os.sched_getaffinity(0))[:3]
    modes = {'yield-16': (16, 0), 'paced-4': (4, 1), 'paced-16': (16, 1), 'paced-32': (32, 1)}
    if args.mode != 'all':
        modes = {args.mode: modes[args.mode]}
    environment = {'revision': revision, 'platform': platform.platform(),
                   'compiler': subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
                   'flags': ' '.join(FLAGS), 'affinity': cpus, 'workers': 2,
                   'timing': 'steady_clock elapsed CPU-side work; includes scheduler delays; no GPU or renderer'}
    (output / 'environment.json').write_text(json.dumps(environment, indent=2) + '\n')
    summary = {}
    with tempfile.TemporaryDirectory(prefix='meridian-lod-build-') as temporary:
        build = Path(temporary)
        (build / 'src').mkdir()
        for name in ('world.cpp', 'world.h', 'world_geometry.cpp', 'world_geometry.h', 'world_streamer.cpp', 'world_streamer.h', 'mc_math.h'):
            (build / 'src' / name).write_bytes(subprocess.check_output(['git', '-C', str(root), 'show', revision + ':src/' + name]))
        executable = build / 'benchmark'
        subprocess.run([args.compiler, *FLAGS, str(HERE / 'benchmark.cpp'), str(build / 'src/world.cpp'),
                        str(build / 'src/world_geometry.cpp'), str(build / 'src/world_streamer.cpp'),
                        '-I', str(build), '-o', str(executable)], check=True)
        for label, (limit, paced) in modes.items():
            child = subprocess.Popen([str(executable), str(limit), str(paced)], stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True,
                                     preexec_fn=lambda: os.sched_setaffinity(0, set(cpus)))
            try:
                stdout, stderr = child.communicate(timeout=180)
            except subprocess.TimeoutExpired:
                child.kill()
                child.communicate()
                raise
            (output / (label + '.txt')).write_text(stdout)
            (output / (label + '.stderr.txt')).write_text(stderr)
            if child.returncode:
                raise RuntimeError(f'{label} exited with {child.returncode}: {stderr}')
            summary[label] = parse_output(stdout)
            (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(label, stdout.strip(), flush=True)
    print('Results:', output)


if __name__ == '__main__':
    main()
