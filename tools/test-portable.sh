#!/usr/bin/env bash
# Simulation and synthesis checks only. The shipped game is built by build.bat.
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${CXX:-c++}"
configuration=portable
flags=(-std=c++20 -O2 -g -Wall -Wextra -Wpedantic -pthread)
if [[ "${1:-}" == --sanitize ]]; then
    configuration=portable-sanitized
    flags=(-std=c++20 -O1 -g -Wall -Wextra -Wpedantic -pthread -fno-omit-frame-pointer
        -fsanitize=address,undefined)
    shift
fi
if (( $# != 0 )); then
    printf 'Usage: %s [--sanitize]\n' "$0" >&2
    exit 2
fi
output="$repo/build/$configuration"
mkdir -p -- "$output"

printf 'Compiling portable simulation and synthesis tests with %s\n' "$compiler"
"$compiler" "${flags[@]}" "$repo/tests/world_tests.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" -o "$output/world_tests"
"$compiler" "${flags[@]}" "$repo/tests/world_streamer_tests.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/world_streamer.cpp" -o "$output/world_streamer_tests"
"$compiler" "${flags[@]}" "$repo/tests/world_lod_geometry_tests.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" -o "$output/world_lod_geometry_tests"
"$compiler" "${flags[@]}" "$repo/tests/world_lod_streaming_tests.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/world_streamer.cpp" -o "$output/world_lod_streaming_tests"
"$compiler" "${flags[@]}" "$repo/tests/render_visibility_tests.cpp" -o "$output/render_visibility_tests"
"$compiler" "${flags[@]}" "$repo/tests/render_timing_tests.cpp" -o "$output/render_timing_tests"
"$compiler" "${flags[@]}" "$repo/tests/game_tests.cpp" "$repo/src/game.cpp" "$repo/src/pedestrians.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/visuals.cpp" -o "$output/game_tests"
"$compiler" "${flags[@]}" "$repo/tests/pedestrian_tests.cpp" "$repo/src/game.cpp" "$repo/src/pedestrians.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/visuals.cpp" -o "$output/pedestrian_tests"
"$compiler" "${flags[@]}" "$repo/tests/workshop_lighting_tests.cpp" "$repo/src/game.cpp" "$repo/src/pedestrians.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/visuals.cpp" -o "$output/workshop_lighting_tests"
"$compiler" "${flags[@]}" "$repo/tests/pedestrian_visuals_tests.cpp" "$repo/src/game.cpp" "$repo/src/pedestrians.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/visuals.cpp" -o "$output/pedestrian_visuals_tests"
"$compiler" "${flags[@]}" "$repo/tests/audio_tests.cpp" -o "$output/audio_tests"
"$compiler" "${flags[@]}" "$repo/tests/world_audio_tests.cpp" -o "$output/world_audio_tests"
"$compiler" "${flags[@]}" "$repo/tests/audio_output_tests.cpp" -o "$output/audio_output_tests"
"$compiler" "${flags[@]}" "$repo/tests/world_audio_scene_tests.cpp" "$repo/src/game.cpp" "$repo/src/pedestrians.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/visuals.cpp" -o "$output/world_audio_scene_tests"
"$compiler" "${flags[@]}" "$repo/tests/world_audio_integration_tests.cpp" "$repo/src/game.cpp" "$repo/src/pedestrians.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" "$repo/src/visuals.cpp" -o "$output/world_audio_integration_tests"
"$compiler" "${flags[@]}" "$repo/tests/cinematics_tests.cpp" "$repo/src/world.cpp" "$repo/src/world_geometry.cpp" -o "$output/cinematics_tests"

for suite in world world_streamer world_lod_geometry world_lod_streaming render_visibility render_timing game pedestrian workshop_lighting pedestrian_visuals audio world_audio audio_output world_audio_scene world_audio_integration cinematics; do
    printf 'Running %s tests\n' "$suite"
    "$output/${suite}_tests"
done
printf 'All portable tests passed. Windows renderer and device checks require the native build.\n'
