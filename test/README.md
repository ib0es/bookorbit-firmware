# Tests

Run from the `firmware/` folder.

| Command | What it checks | Needs |
|---|---|---|
| `python3 test/host/test_cover_jpeg.py` | Firmware-local JPEG decoder compared with libjpeg on high-contrast edges; catches wrapped luminance samples without changing FreeInk SDK | C compiler, python3, Pillow |
| `python3 test/host/test_cover_resampling.py` | Streaming JPEG cover scaling: enlargement, reduction, mixed axes, white fields, alternating rows, halftone density and checkerboard regression | C++17 compiler, python3 |
| `test/host/run.sh` | Pure logic: OPDS stream parser (a book in several formats), book formats (MIME types, extensions, which one opens first, compact download addresses), EPUB CFI parsing and XHTML walking, the layout engine's character count (checked against FreeInkBook itself on every chapter and id anchor of the sample books), Book Orbit request/response format (fixtures are real server responses), progress sync decisions (three-way merge, last change wins), ISO-8601 / HTTP date parsing, the Russian and English hyphenation patterns, FB2 to EPUB conversion (a made-up book with every FB2 element, windows-1251; then every `test/books/*.fb2`, opened and laid out by the engine), MOBI to EPUB (a made-up Mobipocket book with loose HTML, page breaks, filepos links, a contents page, images, windows-1252, and the DRM / HUFF refusals; then every `test/books/*.mobi` and `*.azw3` — KF8 is tested only on those local books) | a C++17 compiler |
| `make -f test/sim/Makefile test` | Whole firmware in the headless simulator, with the **real** network client (`src/net/bookorbit_client.cpp`) talking to an in-process fake Book Orbit server: 53 end-to-end scenarios (pull/push/conflicts, one book in several formats with a position each, the format list of a book, the book menu sync, low memory, sleep and wake, cancel with BACK at every network step, Wi-Fi down, server errors, expired tokens, web-reader CFI positions, downloads), and reader position round-trips on the FreeInkBook reader (percent and EPUB CFI keep the page, the xpath the chapter) on every page of the sample books with every chapter laid out, plus a real CFI from the Book Orbit web reader | C++17 compiler, python3 |
| `python3 test/sim/run_x4_walkthrough.py` (part of the line above) | The X4's screens and keys, one case each: page turns saved while the reader is idle, the critical-battery sleep and USB power, a catalog.json over 50 KB and a corrupt progress.json, "All books" order and the selection after a delete, held keys and pages in long lists, the jump back after a chapter jump, UP/DOWN turning pages, the Appearance preview, fast refreshes, language and the settings file, the Cyrillic keyboard, caches of books in subfolders, downloads leaving no `.part`, a renewed token saved | (built by the line above) |
| `test/sim/build-x4/engine_shots <sd> <out> <book> [page\|n\|p\|%pct\|c<toc>...]` | Renders pages of a book with the FreeInkBook reader into PGM files (for looking at the layout) | (built by the line above) |
| `make -f test/sim/Makefile test/sim/build-x4/cover_shots` then `test/sim/build-x4/cover_shots <scratch sd> <existing out dir> <book path on sd>` | Renders an EPUB cover as thumbnail and sleep screen through the real decoder, including progressive JPEG conversion; scratch SD must contain `covers/`, where `probe.jpg` is replaced | C++17 compiler |
| `test/coverage.sh` | Both of the above built with gcov; prints line coverage of the sync code and fails below 95% (`--show-missed` lists uncovered lines) | g++/gcov, python3 |

Scenarios are listed at the top of `test/sim/run_sync_scenarios.py`;
`python3 test/sim/run_sync_scenarios.py --log "<part of a name>"` prints the firmware log of one.

`test/sim/build/x4_headless <frames dir> <sd root> "<script>"` can also be used by hand: it
runs the firmware with a scripted sequence of buttons (`U D L R C B P`, `w` = wait 2.2 s,
`S` = apply `<sd root>/server_next.json` on the fake server, `Z` = sleep and wake up) and
writes every screen update as a PGM image into `<frames dir>`. `H<key>` holds an arrow key for 3 s, `X` replaces `config.json` with `<sd root>/config_next.json`; `SIM_BATTERY_PCT`, `SIM_BATTERY_VOLTS`, `SIM_USB=1` and `SIM_BATTERY_CHECK_MS` set the simulated battery. The fake server's state
(progress records with CFI, injected failures) lives in `<sd root>/server.json`, see the
comment at the top of `test/sim/fake_bookorbit.cpp`.

`tools/probe_progress.py [bookId]` polls the real server's progress for a book (login
from `include/secrets.h`) and prints every change, to see what the server really sends.

How progress sync works is described at the top of `src/core/progress_sync.h`.

## Hardware TCP-window experiment

The normal `x4` and `picco` environments retain the SDK's 5760-byte receive
window. Two opt-in environments rebuild the five lwIP objects that depend on
receive-window / receive-mailbox settings, from the exact ESP-IDF 5.5.5 lwIP
revision. The original SDK archives are not modified. The first build downloads
a checksum-verified source archive from Espressif into `.pio/lwip-window/`.

```sh
pio run -e picco_tcp_window          # 32768-byte window, 25 receive mailbox slots
pio run -e x4_tcp_window             # 16384-byte window, 14 receive mailbox slots
pio run -e picco_tcp_window -t upload
```

Confirm `[NetPerf] TCP receive window: 32768 bytes` (Picco) or `16384 bytes`
(X4) after starting a network operation. The value comes from the rebuilt TCP
object, so an accidentally linked stock archive cannot pass this check.
The build's `tcp-window/manifest.json` records the source revision and settings.

Compare the same book on the same network with the normal and experimental
builds: record HTTP-header time, body bytes/time, SD-write time, RSSI and heap.
Repeat the download, cancel once during transfer, and update the catalog to
check memory pressure, especially on the X4. Window scaling and SDK structure
layouts remain unchanged; do not enable the experiment by adding a define to a
normal environment. Return to the normal build with `pio run -e picco -t upload`
(or `-e x4`). No speed improvement is assumed until measured on hardware.

Measured on Picco on 2026-11-14, same 2,870,166-byte EPUB: the previous build
transferred the body in 33.194 s; the experimental 32768-byte window / 25-slot
mailbox transferred it in 5.340 s (~6.2x faster), with RSSI -60 dBm versus -49 dBm
previously. HTTP-header waiting and post-download progress/cover requests remain
separate latency sources. This validates the transfer-speed experiment on Picco.

X4 hardware run on the same date, 16384-byte window / 14-slot mailbox: the same
EPUB body transferred in 11.163 s (SD writes 1.714 s), after 17.353 s waiting for
HTTP headers. All 35 catalog books were fetched, but minimum free heap fell below
1 KB during sync. After the download the reader could not allocate its buffers
with 85 KB free / 35 KB largest block and invoked its existing restart-to-book
recovery path. A subsequent stock-window (5760-byte) X4 run transferred the same
file in 37.366 s (SD writes 1.907 s), versus 11.163 s for the larger window
(~3.35x faster). The stock run also failed to index the book after download:
88 KB free / 57 KB largest block were insufficient for simultaneous contiguous
48 KB and 24 KB buffers. Its restart-to-book recovery then opened the book
successfully. Thus the observed restart is not specific to the enlarged TCP
window; the larger window's lower transient heap reserve still needs attention.
The stock run skipped the preceding full catalog sync, so it does not provide
an equivalent comparison of peak catalog memory pressure.

The subsequent X4 reader-memory fix releases library lists before opening a
book and restores them on return. Index construction and foreground chapter
parsing borrow the display buffer exclusively; foreground layout adapts its
arena to the largest available heap block. Image rendering can release the page
record and font caches after painting text, keeping only image placements for
decoding. Background layout never borrows display storage.

With this fix installed, the X4 opened the downloaded book and rendered chapter
text and PNG headers without restarting; page turns also succeeded. The user
confirmed that both text and images were visible. Device logs show image decode
times of approximately 100–109 ms and no allocation failures in that reading run.
This validates reader opening, not the catalog's transient heap reserve.

Simulator comparison of all 1187 pages of the same EPUB produced identical
framebuffer hashes with normal rendering and forced low-memory image rendering
(61 detached image passes). The existing one percent-position mismatch and two
CFI round-trip mismatches were unchanged between normal and constrained runs.
Three library-release/rebuild/failure-recovery scenarios also passed. Both
`x4_tcp_window` and `picco_tcp_window` builds succeeded.
