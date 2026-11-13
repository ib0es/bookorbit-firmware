#!/usr/bin/env bash
# Formats the repo in place.
#   tools/format.sh          C/C++: only lines changed since LINT_BASE (default: merge base with origin/main),
#                            Python and web-flasher: whole files
#   tools/format.sh FILE...  C/C++ files given: formatted whole
source "$(dirname "$0")/lint/common.sh"

if [ $# -gt 0 ]; then
    need clang-format
    clang-format -i "$@"
    exit 0
fi

need git-clang-format; need ruff
base="$(lint_base)"
section "clang-format (lines changed since ${base:0:12})"
git-clang-format --force --extensions "$CPP_EXT" "$base" -- "${CPP_PATHS[@]}" || true

section "ruff"
ruff check --fix . || true
ruff format .

section "web-flasher"
if [ -d web-flasher/node_modules ]; then
    (cd web-flasher && npm run --silent lint:fix; npm run --silent format)
else
    echo "skipped: run npm ci in web-flasher/"
fi
