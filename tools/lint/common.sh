# Shared by tools/lint.sh and tools/format.sh (sourced, not run).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# C/C++ sources owned by this repo (third-party and generated files: .clang-format-ignore).
CPP_PATHS=(src include test/host test/sim simulator/src simulator/include)
CPP_EXT="c,cc,cpp,h,hpp"

# Tracked C/C++ files (clang-format skips the ones in .clang-format-ignore).
cpp_files() {
    local specs=() p e
    for p in "${CPP_PATHS[@]}"; do for e in ${CPP_EXT//,/ }; do specs+=("$p/*.$e"); done; done
    git ls-files -- "${specs[@]}"
}

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: $1 not found. Install the tools: pip install -r tools/lint/requirements.txt" >&2
        exit 2
    fi
}

section() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
