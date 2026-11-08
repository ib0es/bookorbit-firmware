#!/bin/bash
# Line coverage of the progress-sync code (host unit tests + simulator scenarios).
# Usage (from the firmware/ folder):  test/coverage.sh [--show-missed] [--min 95]
set -e
cd "$(dirname "${BASH_SOURCE[0]}")/.."
COV_BUILD=test/sim/build-cov
rm -rf test/build-cov-host
find $COV_BUILD -name '*.gcda' -delete 2>/dev/null || true
COVERAGE_DIR="$PWD/test/build-cov-host" test/host/run.sh
make -j4 -f test/sim/Makefile BUILD=$COV_BUILD OPT="-O0 --coverage" LDEXTRA=--coverage all
X4_HEADLESS=$COV_BUILD/x4_headless python3 test/sim/run_sync_scenarios.py
python3 test/coverage.py "$@" test/build-cov-host $COV_BUILD
