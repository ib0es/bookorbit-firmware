# Tests

Run from the `firmware/` folder.

| Command | What it checks | Needs |
|---|---|---|
| `test/host/run.sh` | Pure logic: OPDS stream parser (a book in several formats), book formats (MIME types, extensions, which one opens first, compact download addresses), EPUB CFI parsing and XHTML walking, Book Orbit request/response format (fixtures are real server responses), progress sync decisions (three-way merge, last change wins), ISO-8601 / HTTP date parsing; on Linux also the miniz workspace (allocated only while unpacking, clean failure without memory) | a C++17 compiler |
| `make -f test/sim/Makefile test` | Whole firmware in the headless simulator, with the **real** network client (`src/net/bookorbit_client.cpp`) talking to an in-process fake Book Orbit server: 52 end-to-end scenarios (pull/push/conflicts, one book in several formats with a position each, the format list of a book, the book menu sync, low memory, sleep and wake, cancel with BACK at every network step, Wi-Fi down, server errors, expired tokens, web-reader CFI positions, downloads), and reader position round-trips (percent, xpath and EPUB CFI) on every page of the sample books, plus a real CFI from the Book Orbit web reader | C++17 compiler, python3 |
| `test/coverage.sh` | Both of the above built with gcov; prints line coverage of the sync code and fails below 95% (`--show-missed` lists uncovered lines) | g++/gcov, python3 |

Scenarios are listed at the top of `test/sim/run_sync_scenarios.py`;
`python3 test/sim/run_sync_scenarios.py --log "<part of a name>"` prints the firmware log of one.

`test/sim/build/x4_headless <frames dir> <sd root> "<script>"` can also be used by hand: it
runs the firmware with a scripted sequence of buttons (`U D L R C B P`, `w` = wait 2.2 s,
`S` = apply `<sd root>/server_next.json` on the fake server, `Z` = sleep and wake up) and
writes every screen update as a PGM image into `<frames dir>`. The fake server's state
(progress records with CFI, injected failures) lives in `<sd root>/server.json`, see the
comment at the top of `test/sim/fake_bookorbit.cpp`.

`tools/probe_progress.py [bookId]` polls the real server's progress for a book (login
from `include/secrets.h`) and prints every change, to see what the server really sends.

How progress sync works is described at the top of `src/services/progress_sync.h`.
