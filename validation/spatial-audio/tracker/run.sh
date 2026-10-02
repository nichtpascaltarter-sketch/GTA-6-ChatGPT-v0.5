#!/usr/bin/env bash
# Portable evidence only; native Windows build remains build.bat.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd -- "$repo"
compiler="${CXX:-c++}"
output="$repo/build/world-audio-validation"
mkdir -p -- "$output"
sources=(tests/world_audio_scene_tests.cpp src/game.cpp src/pedestrians.cpp src/world.cpp src/world_geometry.cpp src/visuals.cpp)
common=(-std=c++20 -g -Wall -Wextra -Wpedantic -Werror -pthread)
"$compiler" "${common[@]}" -O2 "${sources[@]}" -o "$output/world_audio_scene_tests"
"$output/world_audio_scene_tests"
"$compiler" "${common[@]}" -O1 -fno-omit-frame-pointer -fsanitize=address,undefined "${sources[@]}" -o "$output/world_audio_scene_tests_sanitized"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$output/world_audio_scene_tests_sanitized"
