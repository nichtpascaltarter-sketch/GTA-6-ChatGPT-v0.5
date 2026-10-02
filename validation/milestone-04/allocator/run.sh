#!/usr/bin/env bash
# Diagnostic-only builds. No test binary or copied renderer is kept in the repo.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "$here/../../.." && pwd)"
compiler="${CXX:-c++}"
workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT
log_command() { local separator=''; for argument in "$@"; do printf '%s%q' "$separator" "$argument"; separator=' '; done; printf '\n'; }

python3 - "$repo/src/renderer.cpp" "$workspace/arena_under_test.h" <<'PYEXTRACT'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
start = source.index('struct ArenaRange')
end = source.index('\n}\nstruct Renderer::Impl', start)
Path(sys.argv[2]).write_text(source[start:end] + '\n')
PYEXTRACT

{
    printf 'Source revision: '
    git -C "$repo" rev-parse HEAD
    printf 'Compiler: '
    "$compiler" --version | head -n 1
    printf 'Source checksums:\n'
    (cd -- "$repo" && sha256sum src/renderer.cpp src/world.cpp src/world.h)
    printf 'Extracted allocator checksum: '
    sha256sum "$workspace/arena_under_test.h" | cut -d ' ' -f 1
    printf 'Randomized compile: '
    log_command "$compiler" -std=c++20 -O2 -fsanitize=undefined,address -I "$workspace" "$here/randomized.cpp" -o "$workspace/randomized"
    printf 'Randomized run: '; log_command "$workspace/randomized"
    printf 'Route compile: '
    log_command "$compiler" -std=c++20 -O2 -I "$workspace" -I "$repo/src" "$here/route.cpp" "$repo/src/world.cpp" -o "$workspace/route"
    printf 'Route run: '; log_command "$workspace/route"
} > "$here/invocations.log"

"$compiler" -std=c++20 -O2 -fsanitize=undefined,address -I "$workspace" "$here/randomized.cpp" -o "$workspace/randomized"
"$workspace/randomized" > "$here/randomized.log" 2>&1
"$compiler" -std=c++20 -O2 -I "$workspace" -I "$repo/src" "$here/route.cpp" "$repo/src/world.cpp" -o "$workspace/route"
"$workspace/route" > "$here/route.log" 2>&1
cat "$here/invocations.log" "$here/randomized.log" "$here/route.log"
