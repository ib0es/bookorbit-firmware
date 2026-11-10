#!/usr/bin/env bash
# Reader timings from a device log: page draws (with glyph cache counts),
# chapter layouts, memory with a book open, and a summary.
#
# Usage (from firmware/):
#   tools/reader_timings.sh                 # the newest log in logs/ (pio device monitor, log2file)
#   tools/reader_timings.sh path/to/x4.log
#   pio device monitor | tee x4.log         # then run it on x4.log
set -euo pipefail

log="${1:-$(ls -t logs/*.log 2>/dev/null | head -1 || true)}"
if [ -z "$log" ] || [ ! -f "$log" ]; then
    echo "No log: give a file, or run 'pio device monitor' with the log2file filter (logs/)." >&2
    exit 1
fi
echo "== $log"

echo; echo "-- memory"
grep -a "\[Mem\] book opened" "$log" || echo "(none)"

echo; echo "-- chapters laid out"
grep -a -E "Chapter [0-9]+: first page in|laid out in the background|Chapter [0-9]+ laid out|laying out ~" "$log" || echo "(none)"

echo; echo "-- pages drawn"
grep -a "Page drawn in" "$log" || echo "(none)"

echo; echo "-- summary"
grep -a "Page drawn in" "$log" | awk '
    {
        for (i = 1; i <= NF; i++) {
            if ($i == "in" && $(i - 1) == "drawn") { ms = $(i + 1) + 0 }
            if ($i == "text") { t = $(i + 1); gsub(/[^0-9]/, "", t); text = t + 0 }
            if ($i ~ /^rasterized/) r = $(i - 1) + 0
            if ($i == "cached,") c = $(i - 1) + 0
            if ($i == "each),") { u = $(i - 2); gsub(/[^0-9]/, "", u); us += (u + 0) * r }
        }
        n++; sum += ms; tsum += text; rs += r; cs += c; r = 0
        if (ms > max) max = ms
        if (n == 1) first = ms
    }
    END {
        if (n == 0) { print "(no pages)"; exit }
        printf "%d pages: first %d ms, average %.0f ms (text %.0f ms), slowest %d ms\n", n, first, sum / n, tsum / n, max
        printf "glyphs: %d rasterized (%.0f us each), %d from the cache (%.0f%% hits)\n", rs, rs ? us / rs : 0, cs,
               (rs + cs) ? 100 * cs / (rs + cs) : 0
    }'
grep -a -E "\[Event\] Handled in" "$log" | awk '{ for (i = 1; i <= NF; i++) if ($i == "in") { v = $(i + 1) + 0; n++; s += v; if (v > m) m = v } }
    END { if (n) printf "key presses: %d handled, average %.0f ms, slowest %d ms (with the e-ink refresh)\n", n, s / n, m }'
