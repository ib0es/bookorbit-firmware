#!/usr/bin/env python3
"""Line coverage of the progress-sync code, merged from host unit tests and the
headless simulator scenarios. Run from the firmware/ folder:  test/coverage.sh

Prints per-file line coverage and the total; exits 1 if the total is below
--min (default 95).
"""
import argparse, glob, gzip, json, os, subprocess, sys, tempfile

# The code that decides, stores and transfers reading progress.
TARGETS = [
    "src/core/progress_sync.cpp",
    "src/core/time_utils.cpp",
    "src/services/sync_clock.cpp",
    "src/storage/progress_store.cpp",
    "src/services/sync_service.cpp",
    "src/services/sync_common.cpp",
    "src/services/sync_book.cpp",
    "src/services/sync_download.cpp",
    "src/services/sync_full.cpp",
    "src/services/sync_background.cpp",
    "src/core/bookorbit_protocol.cpp",
    "src/core/opds_stream_parser.cpp",
    "src/core/book_formats.cpp",
    "src/core/epub_cfi.cpp",
    "src/reader/engine/engine_text_counter.cpp",
]
# Shown for information, not counted: the HTTP/Wi-Fi transport also serves the
# settings screens (connection test, Wi-Fi), which these tests do not drive.
INFO = ["src/net/bookorbit_client.cpp", "src/services/library_manager.cpp", "src/services/library_covers.cpp",
        "src/services/library_cleanup.cpp", "src/services/library_progress.cpp", "src/reader/book_reader.cpp",
        "src/reader/book_reader_layout.cpp", "src/reader/book_reader_position.cpp", "src/reader/book_reader_render.cpp",
        "src/reader/book_reader_preview.cpp", "src/reader/book_reader_cover.cpp",
        "src/reader/engine/sd_book_storage.cpp", "src/reader/engine/bitmap_book_font.cpp"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--min", type=float, default=95.0)
    ap.add_argument("--show-missed", action="store_true")
    ap.add_argument("dirs", nargs="+")
    a = ap.parse_args()
    root = os.getcwd()
    hits = {}   # file -> {line: count}
    for d in a.dirs:
        for gcda in glob.glob(os.path.join(d, "**", "*.gcda"), recursive=True):
            tmp = tempfile.mkdtemp()
            subprocess.run(["gcov", "-j", "-o", os.path.dirname(os.path.abspath(gcda)), os.path.abspath(gcda)],
                           cwd=tmp, capture_output=True)
            for js in glob.glob(os.path.join(tmp, "*.gcov.json.gz")):
                data = json.load(gzip.open(js))
                cwd = data.get("current_working_directory", "")
                for f in data["files"]:
                    path = f["file"]
                    if not os.path.isabs(path): path = os.path.join(cwd, path)
                    rel = os.path.relpath(os.path.realpath(path), os.path.realpath(root))
                    if rel not in TARGETS and rel not in INFO: continue
                    lines = hits.setdefault(rel, {})
                    for ln in f["lines"]:
                        lines[ln["line_number"]] = lines.get(ln["line_number"], 0) + ln["count"]
    tot = cov = 0
    print("%-36s %8s %7s" % ("file", "lines", "cover"))
    for t in TARGETS:
        lines = hits.get(t, {})
        n = len(lines); c = sum(1 for v in lines.values() if v > 0)
        tot += n; cov += c
        print("%-36s %4d/%-4d %6.1f%%" % (t, c, n, 100.0 * c / n if n else 0.0))
        if a.show_missed and n:
            missed = sorted(l for l, v in lines.items() if v == 0)
            if missed: print("    missed:", ",".join(map(str, missed)))
    pct = 100.0 * cov / tot if tot else 0.0
    print("%-36s %4d/%-4d %6.1f%%" % ("TOTAL", cov, tot, pct))
    for t in INFO:
        lines = hits.get(t, {})
        n = len(lines); c = sum(1 for v in lines.values() if v > 0)
        print("%-36s %4d/%-4d %6.1f%%  (not counted)" % (t, c, n, 100.0 * c / n if n else 0.0))
    if pct < a.min:
        print("coverage %.1f%% is below %.1f%%" % (pct, a.min)); sys.exit(1)

if __name__ == "__main__":
    main()
