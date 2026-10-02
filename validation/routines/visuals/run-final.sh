#!/usr/bin/env bash
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd -- "$repo"
compiler="${CXX:-c++}"
output="$repo/build/resident-visual-validation"
mkdir -p -- "$output"
sources=(tests/pedestrian_visuals_tests.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp)
flags=(-std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror)
"$compiler" "${flags[@]}" -O2 "${sources[@]}" -o "$output/strict"
"$output/strict"
"$compiler" "${flags[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "${sources[@]}" -o "$output/sanitized"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$output/sanitized"
