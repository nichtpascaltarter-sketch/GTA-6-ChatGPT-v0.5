#!/usr/bin/env bash
# Diagnostic-only builds. No test binary or copied renderer is kept in the repo.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "$here/../../.." && pwd)"
compiler="${CXX:-c++}"
revision=2acd647a877b2062e79580c90c36da90eb4d1cdf
if (( $# > 1 )); then printf 'Usage: %s [output-directory]\n' "$0" >&2; exit 2; fi
output="${1:-$repo/build/allocator-2acd647}"
mkdir -p -- "$output"
workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT
pinned_source="$workspace/src"
mkdir -p -- "$pinned_source"
for file in renderer.cpp world.cpp world.h mc_math.h; do
    git -C "$repo" show "$revision:src/$file" > "$pinned_source/$file"
done
log_command() { local separator=''; for argument in "$@"; do printf '%s%q' "$separator" "$argument"; separator=' '; done; printf '\n'; }

python3 - "$pinned_source/renderer.cpp" "$workspace/arena_under_test.h" <<'PYEXTRACT'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
start = source.index('struct ArenaRange')
end = source.index('\n}\nstruct Renderer::Impl', start)
Path(sys.argv[2]).write_text(source[start:end] + '\n')
PYEXTRACT

{
    printf 'Source revision: '
    printf '%s\n' "$revision"
    printf 'Compiler: '
    "$compiler" --version | head -n 1
    printf 'Source checksums:\n'
    (cd -- "$workspace" && sha256sum src/renderer.cpp src/world.cpp src/world.h src/mc_math.h)
    printf 'Extracted allocator checksum: '
    sha256sum "$workspace/arena_under_test.h" | cut -d ' ' -f 1
    printf 'Randomized compile: '
    log_command "$compiler" -std=c++20 -O2 -fsanitize=undefined,address -I "$workspace" "$here/randomized.cpp" -o "$workspace/randomized"
    printf 'Randomized run: '; log_command "$workspace/randomized"
    printf 'Route compile: '
    log_command "$compiler" -std=c++20 -O2 -I "$workspace" -I "$pinned_source" "$here/route.cpp" "$pinned_source/world.cpp" -o "$workspace/route"
    printf 'Route run: '; log_command "$workspace/route"
} > "$output/invocations.log"

"$compiler" -std=c++20 -O2 -fsanitize=undefined,address -I "$workspace" "$here/randomized.cpp" -o "$workspace/randomized"
"$workspace/randomized" > "$output/randomized.log" 2>&1
"$compiler" -std=c++20 -O2 -I "$workspace" -I "$pinned_source" "$here/route.cpp" "$pinned_source/world.cpp" -o "$workspace/route"
"$workspace/route" > "$output/route.log" 2>&1
cat "$output/invocations.log" "$output/randomized.log" "$output/route.log"
