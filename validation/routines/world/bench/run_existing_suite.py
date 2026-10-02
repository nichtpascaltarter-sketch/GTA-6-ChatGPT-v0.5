import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

root = Path('/workspace/GTA-6-ChatGPT-v0.5-routines')
artifacts = Path('/workspace/scratch/routines-bench-collision')
suite = sys.argv[1]
assert suite in {'world_lod_geometry_tests', 'world_lod_streaming_tests', 'world_streamer_tests'}
sources = [f'tests/{suite}.cpp', 'src/world.cpp', 'src/world_geometry.cpp']
if suite != 'world_lod_geometry_tests':
    sources.append('src/world_streamer.cpp')
tracked = sources + ['src/world.h', 'src/world_geometry.h', 'src/world_streamer.h', 'src/mc_math.h']
def fingerprint():
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in tracked}
command = ['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-pthread', *sources, '-o', str(artifacts / suite)]
record = {
    'suite': suite,
    'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'cwd': str(root),
    'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'compiler': subprocess.check_output(['g++', '--version'], cwd=root, text=True).splitlines()[0],
    'source_sha256_before': fingerprint(),
    'build_command': command,
}
start = time.perf_counter()
with (artifacts / f'{suite}.build.log').open('w') as log:
    build = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=180)
record['build_seconds'] = time.perf_counter() - start
record['build_exit'] = build.returncode
if build.returncode == 0:
    run_command = [str(artifacts / suite)]
    record['run_command'] = run_command
    start = time.perf_counter()
    with (artifacts / f'{suite}.run.log').open('w') as log:
        run = subprocess.run(run_command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    record['run_seconds'] = time.perf_counter() - start
    record['run_exit'] = run.returncode
record['source_sha256_after'] = fingerprint()
record['sources_stable'] = record['source_sha256_before'] == record['source_sha256_after']
record['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(artifacts / f'{suite}.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({key: record[key] for key in ('suite', 'build_seconds', 'build_exit', 'run_seconds', 'run_exit', 'sources_stable') if key in record}))
sys.exit(record.get('run_exit', record['build_exit']) or (0 if record['sources_stable'] else 2))
