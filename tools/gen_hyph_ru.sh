#!/bin/sh
# Rebuilds src/reader/engine/hyph_ru.h (Russian hyphenation built into the firmware)
# from the TeX hyph-utf8 patterns: CTAN hyph-utf8, tex/generic/hyph-utf8/patterns/txt/hyph-ru.pat.txt
# Usage: tools/gen_hyph_ru.sh path/to/hyph-ru.pat.txt
set -e
cd "$(dirname "$0")/.."
python3 freeink-sdk/libs/book/FreeInkBook/tools/hyphc.py "$1" src/reader/engine/hyph_ru.h
