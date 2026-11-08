#!/bin/bash
# Host unit tests for pure-logic modules (no Arduino / hardware needed).
# Usage: test/host/run.sh        (from the firmware/ folder or anywhere)
#        COVERAGE_DIR=dir test/host/run.sh   builds with gcov into dir (test/coverage.sh)
set -e -o pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${COVERAGE_DIR:-$(mktemp -d)}"
mkdir -p "$OUT"
CXX="${CXX:-c++}"
FLAGS="-std=c++17 -Wall -Wextra -O1 -I$ROOT/src -I$ROOT/simulator/include -I$ROOT/simulator/lib_arduinojson/src"
if [ "$(uname)" = "Darwin" ]; then
    FLAGS="$FLAGS -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1"
fi
[ -n "$COVERAGE_DIR" ] && FLAGS="$FLAGS -O0 --coverage"

build() {   # name sources...
    local name=$1; shift
    mkdir -p "$OUT/$name"
    (cd "$OUT/$name" && $CXX $FLAGS "$@" -o "$OUT/$name/test")
}
build i18n     "$ROOT/test/host/test_i18n.cpp" "$ROOT/src/ui/i18n.cpp"
build opds     "$ROOT/test/host/test_opds_stream_parser.cpp" "$ROOT/src/net/opds_stream_parser.cpp"
build cfi      "$ROOT/test/host/test_epub_cfi.cpp" "$ROOT/src/reader/epub_cfi.cpp"
build progress "$ROOT/test/host/test_progress_sync.cpp" "$ROOT/src/services/progress_sync.cpp" "$ROOT/src/services/time_utils.cpp"
build protocol "$ROOT/test/host/test_bookorbit_protocol.cpp" "$ROOT/src/net/bookorbit_protocol.cpp" "$ROOT/src/services/time_utils.cpp"
if [ "$(uname)" = Linux ]; then   # needs GNU ld --wrap
    mkdir -p "$OUT/miniz"
    ${CC:-cc} ${COVERAGE_DIR:+--coverage} -O1 -w -c "$ROOT/src/reader/miniz.c" -o "$OUT/miniz/miniz.o"
    (cd "$OUT/miniz" && $CXX $FLAGS "$ROOT/test/host/test_miniz_workspace.cpp" miniz.o -Wl,--wrap=malloc -o test)
fi

rc=0
echo "== I18n";              "$OUT/i18n/test" || rc=1
echo "== OpdsStreamParser";  "$OUT/opds/test" || rc=1
echo "== EpubCfi";          "$OUT/cfi/test" | tail -1 || rc=1
echo "== ProgressSync";      "$OUT/progress/test" > "$OUT/progress.log" || rc=1; tail -1 "$OUT/progress.log"; [ $rc = 0 ] || cat "$OUT/progress.log"
echo "== BookOrbitProtocol"; "$OUT/protocol/test" | grep -v "^[a-zA-Z/]* *$" || rc=1
if [ -x "$OUT/miniz/test" ]; then
    echo "== miniz workspace"; "$OUT/miniz/test" "$ROOT/simulator/sdcard/books/Alice in Wonderland.epub" | tail -1 || rc=1
fi
[ -z "$COVERAGE_DIR" ] && rm -rf "$OUT"
exit $rc
