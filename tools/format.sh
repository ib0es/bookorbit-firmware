#!/usr/bin/env bash
# Formats the repo in place.
#   tools/format.sh          everything: C/C++, Python, web-flasher
#   tools/format.sh FILE...  only the C/C++ files given
source "$(dirname "$0")/lint/common.sh"

if [ $# -gt 0 ]; then
    need clang-format
    clang-format -i "$@"
    exit 0
fi

need clang-format; need ruff
section "clang-format"
cpp_files | xargs clang-format -i

section "ruff"
ruff check --fix . || true
ruff format .

section "web-flasher"
if [ -d web-flasher/node_modules ]; then
    (cd web-flasher && npm run --silent lint:fix; npm run --silent format)
else
    echo "skipped: run npm ci in web-flasher/"
fi
