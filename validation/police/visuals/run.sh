#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."

# Authored portable checks only. Native D3D12 screenshots are a separate gate.
output="build/police-visual-evidence"
compiler="${CXX:-c++}"
mkdir -p "$output/strict" "$output/sanitized"
units=(game game_law law law_navigation pedestrians world world_geometry visuals)
sources=()
for unit in "${units[@]}"; do sources+=("src/$unit.cpp"); done
sha256sum "${sources[@]}" src/{game,law,law_navigation,pedestrians,world,world_geometry,mc_math}.h \
    tests/{police_visuals,pedestrian_visuals}_tests.cpp \
    validation/police/visuals/non_police_probe.cpp > "$output/source.sha256"

for kind in strict sanitized; do
    flags=(-std=c++20 -Wall -Wextra -Wpedantic -Werror -pthread)
    if [[ "$kind" == sanitized ]]; then
        flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    else
        flags+=(-O2 -g)
    fi
    objects=()
    for unit in "${units[@]}"; do
        object="$output/$kind/$unit.o"
        "$compiler" "${flags[@]}" -c "src/$unit.cpp" -o "$object"
        objects+=("$object")
    done
    for suite in police_visuals pedestrian_visuals; do
        "$compiler" "${flags[@]}" "tests/${suite}_tests.cpp" "${objects[@]}" -o "$output/$kind/$suite"
        ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
            "$output/$kind/$suite" > "$output/$kind/$suite.log" 2>&1
        cat "$output/$kind/$suite.log"
    done
done

# Substitute only the original visual authoring code; current game state and
# all other objects remain identical, so changes in unrelated code cancel out.
git show 70126b84943c431095d556b9b7b2730ad64b9cbc:src/visuals.cpp > "$output/baseline-visuals.cpp"
"$compiler" -std=c++20 -O2 -g -pthread -Isrc -c "$output/baseline-visuals.cpp" -o "$output/baseline-visuals.o"
common=()
for unit in "${units[@]}"; do
    if [[ "$unit" != visuals ]]; then common+=("$output/strict/$unit.o"); fi
done
for variant in baseline current; do
    visual="$output/strict/visuals.o"
    if [[ "$variant" == baseline ]]; then visual="$output/baseline-visuals.o"; fi
    "$compiler" -std=c++20 -O2 -g -pthread -Isrc \
        validation/police/visuals/non_police_probe.cpp "${common[@]}" "$visual" -o "$output/$variant-probe"
    "$output/$variant-probe" > "$output/$variant-probe.log"
done
diff -u "$output/baseline-probe.log" "$output/current-probe.log" > "$output/non-police-diff.log"
sha256sum -c "$output/source.sha256" > "$output/source-verified.log"
printf 'PASS all 23 non-police meshes match the baseline exactly\n'
