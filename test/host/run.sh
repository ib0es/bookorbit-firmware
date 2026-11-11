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
build opds     "$ROOT/test/host/test_opds_stream_parser.cpp" "$ROOT/src/net/opds_stream_parser.cpp" "$ROOT/src/net/book_formats.cpp"
build formats  "$ROOT/test/host/test_book_formats.cpp" "$ROOT/src/net/book_formats.cpp"
build textutil "$ROOT/test/host/test_text_utils.cpp"
build cfi      "$ROOT/test/host/test_epub_cfi.cpp" "$ROOT/src/reader/epub_cfi.cpp"
build cfimap   "$ROOT/test/host/test_cfi_map.cpp" "$ROOT/src/reader/cfi_map.cpp" "$ROOT/src/reader/epub_cfi.cpp"
build progress "$ROOT/test/host/test_progress_sync.cpp" "$ROOT/src/services/progress_sync.cpp" "$ROOT/src/services/time_utils.cpp"
build protocol "$ROOT/test/host/test_bookorbit_protocol.cpp" "$ROOT/src/net/bookorbit_protocol.cpp" "$ROOT/src/services/time_utils.cpp"
build x3sens   "$ROOT/test/host/test_x3_sensors.cpp" "$ROOT/src/services/time_utils.cpp" "$ROOT/src/app/auto_rotate.cpp"
build x4pro    "$ROOT/test/host/test_x4pro_features.cpp"
build x4c      -I"$ROOT/include" -DFREEINK_DEVICE_X4CLASSIC=1 -DFREEINK_CAP_USB_MSC=1 "$ROOT/test/host/test_x4c_features.cpp" "$ROOT/src/app/app_state.cpp" "$ROOT/src/ui/i18n.cpp"
# FreeInkBook (the layout engine) for the tests that check against it: vendored C
# libraries once, the C++ core with each test.
FIB="$ROOT/freeink-sdk/libs/book/FreeInkBook"
FIB_INC="-DFREEINK_BOOK_SMALL=1 -I$FIB/include -I$FIB/third_party/expat -I$FIB/third_party/miniz -I$FIB/third_party/libunibreak -I$FIB/third_party/pngle -I$FIB/third_party/tjpgd -I$FIB/third_party/stb -I$ROOT/freeink-sdk/libs/font/FreeInkFont/include -I$ROOT/freeink-sdk/libs/font/FreeInkFont/third_party/stb"
mkdir -p "$OUT/fib"
for src in miniz_impl expat_xmlparse expat_xmlrole expat_xmltok unibreak_impl pngle_impl tjpgd_impl; do
    ${CC:-cc} -O1 -w $FIB_INC -c "$FIB/src/vendor/$src.c" -o "$OUT/fib/$src.o"
done
FIB_CORE="$FIB/src/FreeInkBook.cpp $FIB/src/BookCatalog.cpp $FIB/src/epub/ZipCatalog.cpp $FIB/src/epub/XmlSax.cpp $FIB/src/epub/PackageParsers.cpp $FIB/src/epub/ImageProbe.cpp $FIB/src/text/EntityFilter.cpp $FIB/src/text/Hyphenator.cpp $FIB/src/css/Css.cpp $FIB/src/layout/ChapterLayout.cpp $FIB/src/cache/PageCache.cpp $FIB/src/render/ImageRenderer.cpp $FIB/src/render/PageRenderer.cpp $ROOT/freeink-sdk/libs/font/FreeInkFont/src/TtfFont.cpp $ROOT/freeink-sdk/libs/font/FreeInkFont/src/Gpos.cpp $ROOT/freeink-sdk/libs/font/FreeInkFont/src/Gsub.cpp"
build_fib() {   # name sources... (with the engine; its own warnings are not ours)
    local name=$1; shift
    mkdir -p "$OUT/$name"
    (cd "$OUT/$name" && $CXX $FLAGS -Wno-unused-parameter $FIB_INC "$@" $FIB_CORE "$OUT"/fib/*.o -o "$OUT/$name/test")
}
build_fib hyph "$ROOT/test/host/test_hyphenation.cpp"
build_fib fb2 "$ROOT/test/host/test_fb2_to_epub.cpp" "$ROOT/src/convert/fb2_to_epub.cpp" "$ROOT/src/convert/zip_writer.cpp"
build_fib mobi "$ROOT/test/host/test_mobi_to_epub.cpp" "$ROOT/src/convert/mobi_to_epub.cpp" "$ROOT/src/convert/fb2_to_epub.cpp" "$ROOT/src/convert/zip_writer.cpp"
build_fib txt "$ROOT/test/host/test_txt_to_epub.cpp" "$ROOT/src/convert/txt_to_epub.cpp" "$ROOT/src/convert/fb2_to_epub.cpp" "$ROOT/src/convert/zip_writer.cpp"
build_fib ttf "$ROOT/test/host/test_ttf_font.cpp" "$ROOT/src/reader/engine/ttf_book_font.cpp" "$ROOT/src/reader/fonts/builtin_font.cpp"
build_fib counter "$ROOT/test/host/test_engine_text_counter.cpp" "$ROOT/src/reader/engine/engine_text_counter.cpp" "$ROOT/src/reader/epub_cfi.cpp"

rc=0
echo "== I18n";              "$OUT/i18n/test" || rc=1
echo "== OpdsStreamParser";  "$OUT/opds/test" || rc=1
echo "== BookFormats";       "$OUT/formats/test" || rc=1
echo "== utf8Prefix";        "$OUT/textutil/test" || rc=1
echo "== EpubCfi";          "$OUT/cfi/test" | tail -1 || rc=1
echo "== CfiMap";           "$OUT/cfimap/test" | grep -v "^ *pub\|^ *loc" | tail -40 || rc=1
echo "== ProgressSync";      "$OUT/progress/test" > "$OUT/progress.log" || rc=1; tail -1 "$OUT/progress.log"; [ $rc = 0 ] || cat "$OUT/progress.log"
echo "== BookOrbitProtocol"; "$OUT/protocol/test" | grep -v "^[a-zA-Z/]* *$" || rc=1
echo "== X3Sensors";         "$OUT/x3sens/test" || rc=1
echo "== X4ProFeatures";     "$OUT/x4pro/test" || rc=1
echo "== X4ClassicFeatures"; "$OUT/x4c/test" || rc=1
echo "== FB2 to EPUB";       "$OUT/fb2/test" "$ROOT/test/books" || rc=1
echo "== MOBI to EPUB";      "$OUT/mobi/test" "$ROOT/test/books" || rc=1
echo "== TXT to EPUB";       "$OUT/txt/test" "$ROOT/test/books" || rc=1
echo "== TrueType fonts";    "$OUT/ttf/test" || rc=1
echo "== Hyphenation";      "$OUT/hyph/test" || rc=1
echo "== EngineTextCounter"; "$OUT/counter/test" "$ROOT/simulator/sdcard/books/Alice in Wonderland.epub" \
    "$ROOT/simulator/sdcard/books/01. Frankenstein (1818).epub" "$ROOT/simulator/sdcard/books/Pride and Prejudice.epub" \
    | grep -v "^units" || rc=1
[ -z "$COVERAGE_DIR" ] && rm -rf "$OUT"
exit $rc
