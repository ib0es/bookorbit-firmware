#!/usr/bin/env bash
# Lint and format check for the whole repo; exits non-zero on any finding.
#   tools/lint.sh            all checks
#   tools/lint.sh cpp|py|web only one part
# Fails on unformatted code anywhere in the repo (clang-format, ruff format, prettier).
source "$(dirname "$0")/lint/common.sh"

part="${1:-all}"
fail=0

if [[ $part == all || $part == cpp ]]; then
    need clang-format; need cppcheck
    section "clang-format"
    if cpp_files | xargs clang-format --dry-run --Werror; then
        echo ok
    else
        echo "-> run tools/format.sh" >&2
        fail=1
    fi

    section "cppcheck (src)"
    cppcheck --enable=warning,performance,portability --std=c++20 --language=c++ \
        -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" --inline-suppr --quiet --error-exitcode=1 \
        -DARDUINO=100 -DPROGMEM= '-DESP_IDF_VERSION_VAL(a,b,c)=0' -DESP_IDF_VERSION=0 -Isrc -Iinclude \
        --suppressions-list=tools/lint/cppcheck-suppressions.txt \
        --template='{file}:{line}: {severity}/{id}: {message}' src && echo ok || fail=1
fi

if [[ $part == all || $part == py ]]; then
    need ruff; need mypy
    section "ruff, mypy"
    ruff check . && ruff format --check . && mypy || fail=1
fi

if [[ $part == all || $part == web ]]; then
    section "web-flasher (eslint, prettier)"
    if [ -d web-flasher/node_modules ]; then
        (cd web-flasher && npm run --silent lint && npm run --silent format:check) || fail=1
    else
        echo "skipped: run npm ci in web-flasher/"
    fi
fi

exit $fail
