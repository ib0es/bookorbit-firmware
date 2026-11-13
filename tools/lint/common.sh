# Shared by tools/lint.sh and tools/format.sh (sourced, not run).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# C/C++ sources owned by this repo (third-party and generated files: .clang-format-ignore).
CPP_PATHS=(src include test/host test/sim simulator/src)
CPP_EXT="c,cc,cpp,h,hpp"

# Base for "changed lines": LINT_BASE, else the merge base with origin/main, else HEAD.
lint_base() {
    if [ -n "${LINT_BASE:-}" ]; then echo "$LINT_BASE"; return; fi
    git merge-base HEAD origin/main 2>/dev/null || echo HEAD
}

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: $1 not found. Install the tools: pip install -r tools/lint/requirements.txt" >&2
        exit 2
    fi
}

section() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
