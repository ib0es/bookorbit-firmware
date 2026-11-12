#!/usr/bin/env python3
"""X4 walkthrough: the device's functions driven with its keys, one case each.

    make -f test/sim/Makefile all
    python3 test/sim/run_x4_walkthrough.py            (VERBOSE=1: the log of a failed case)
    python3 test/sim/run_x4_walkthrough.py "<part of a name>"   (only those cases)

Every case runs the headless runner on a fresh copy of simulator/sdcard (with
test/sim/config.json), after an optional setup of the card, and checks the log
and the files the steps must have left. It covers what the sync scenarios
(run_sync_scenarios.py) do not: the screens and keys, the power rules, the
files on the card. Navigation (X4): home rows 0 book, 1 catalog, 2 settings,
3 sync; reader menu 0 contents, 1 appearance, 2 bookmarks, 3 sync, 4 resume,
5 close; settings 0 language, 1 Wi-Fi, 2 server, 3 sleep cover, 4 settings
file, 5 info, 6 sync, 7 orientation; catalog 0 search, 1 all books ...
"""
import glob, json, os, re, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.environ.get("X4_HEADLESS") or os.path.join(ROOT, "test/sim/build-x4/x4_headless")
SCREEN_W = 528 if (os.environ.get("DEVICE") == "x3" or "build-x3" in BIN) else 480


def kb_tap(x, y):
    return f"T{round(x * SCREEN_W / 480)},{y};"

ALL_BOOKS = "DC" + "DC"            # home -> catalog -> "All books"
OPEN_BOOK = "C"                    # home: the book on the card
READER_MENU = "C"                  # reading: the book menu
APPEARANCE = READER_MENU + "DC"    # reading -> menu -> Appearance
SETTINGS = "DDC"                   # home -> settings
SETTINGS_FILE = SETTINGS + "DDDDC" # -> "Settings file" submenu
HOME_SYNC = "UCww"                 # home: "Sync now"
SORTED = ["01. Frankenstein (1818)", "Alice in Wonderland", "Frankenstein", "Pride and Prejudice",
          "Тестовая книга", "Moby Dick", "The Time Machine"]

# ---------------------------------------------------------------- helpers
def cfg(sd):
    return json.load(open(os.path.join(sd, "config.json")))

def merge_config(sd, **kv):
    p = os.path.join(sd, "config.json")
    c = json.load(open(p)); c.update(kv)
    json.dump(c, open(p, "w"), ensure_ascii=False)

def pages(log):
    return [int(p) for p in re.findall(r"\[Reader\] Page (\d+)/\d+", log)]

def library_rows(log):
    return re.findall(r"\[UI\] Library row (\d+)/(\d+): '([^']*)' (on card|cloud|folder)", log)

def after(log, marker):
    i = log.rfind(marker)
    return log[i:] if i >= 0 else ""

def server(sd, state):
    json.dump(state, open(os.path.join(sd, "server.json"), "w"))

def files(sd, pattern):
    return glob.glob(os.path.join(sd, pattern), recursive=True)

# ---------------------------------------------------------------- setups
def big_catalog(sd):
    """A catalog.json well over the 50 KB that SDCardManager::readFile stops at."""
    books = [{"id": str(1000 + i), "fileId": 5000 + i,
              "title": "Очень длинное название книги номер %d из большой библиотеки" % i,
              "author": "Автор с длинным именем %d" % i, "series": "Серия %d" % (i % 20), "seriesIdx": i % 7,
              "formats": [{"f": 5000 + i, "k": 1}], "cover": "/api/v1/opds/%d/thumbnail" % (1000 + i),
              "progress": 0, "page": 1} for i in range(400)]
    json.dump(books, open(os.path.join(sd, "books/catalog.json"), "w"), ensure_ascii=False)
    assert os.path.getsize(os.path.join(sd, "books/catalog.json")) > 60000

def corrupt_progress(sd):
    open(os.path.join(sd, "books/progress.json"), "w").write('{"version":1,"books":[{"path":')

def same_titles(sd):
    """Two server books of one title, and the file an older firmware downloaded
    for them under the title alone (with its cache, bookmarks and position)."""
    alice = lambda i, f: {"id": str(i), "fileId": f, "title": "Alice in Wonderland", "author": "A%d" % i,
                          "formats": [{"f": f, "k": 1}], "cover": ""}
    json.dump([alice(1, 101), alice(9, 909)], open(os.path.join(sd, "books/catalog.json"), "w"))
    old = "/books/Alice in Wonderland.epub"
    json.dump({"version": 1, "books": [{"fileId": 909, "bookId": 9, "path": old, "title": "Alice in Wonderland",
                                        "local": {"p": 0.25}}]}, open(os.path.join(sd, "books/progress.json"), "w"))
    json.dump({"filePath": old, "title": "Alice in Wonderland", "fileId": 909, "progress": 0.25, "page": 1,
               "totalPages": 1}, open(os.path.join(sd, "books/last_book.json"), "w"))
    os.makedirs(os.path.join(sd, "bookmarks"), exist_ok=True)
    json.dump({"bookmarks": [{"spine": 1, "chars": 0, "percent": 0.02, "chapter": "c", "snippet": "s"}]},
              open(os.path.join(sd, "bookmarks/Alice in Wonderland.epub.json"), "w"))
    os.makedirs(os.path.join(sd, "cache/Alice in Wonderland.epub.fib"), exist_ok=True)
    open(os.path.join(sd, "cache/Alice in Wonderland.epub.fib/marker"), "w").write("x")

def same_titles_split(log, sd):
    rec = json.load(open(os.path.join(sd, "books/progress.json")))["books"]
    last = json.load(open(os.path.join(sd, "books/last_book.json")))
    new = "/books/Alice in Wonderland [9].epub"
    return (os.path.exists(sd + new) and not os.path.exists(sd + "/books/Alice in Wonderland.epub")
            and not os.path.exists(sd + "/books/Alice in Wonderland [1].epub")
            and "Master list: 6 books (2 in the catalog" in log
            and [r["path"] for r in rec if r.get("fileId") == 909] == [new]
            and last["filePath"] == new
            and os.path.exists(os.path.join(sd, "bookmarks/Alice in Wonderland [9].epub.json"))
            and os.path.exists(os.path.join(sd, "cache/Alice in Wonderland [9].epub.fib/marker")))

def progress_only_tmp(sd):
    """A save cut between replaceFile's remove and rename: only the complete .tmp left."""
    json.dump({"version": 1, "books": [{"fileId": 101, "bookId": 1, "path": "/books/Alice in Wonderland.epub",
                                        "title": "Alice in Wonderland", "local": {"p": 0.5}}]},
              open(os.path.join(sd, "books/progress.json.tmp"), "w"))

def tmp_taken(log, sd):
    return ("its complete .tmp taken" in log and "Loaded 1 book records" in log
            and os.path.exists(os.path.join(sd, "books/progress.json"))
            and not os.path.exists(os.path.join(sd, "books/progress.json.tmp")))

def subfolder_book(sd):
    os.makedirs(os.path.join(sd, "books/Sub"), exist_ok=True)
    shutil.copy(os.path.join(sd, "books/Alice in Wonderland.epub"), os.path.join(sd, "books/Sub/Alice in Wonderland.epub"))

def russian(sd):
    merge_config(sd, language="ru")

def clock_redraw_fast(log, sd):
    # After the first page, the minute's redraw (whenever it comes) and no full refresh.
    first = log.find("[Reader] Page drawn")
    rest = log[first + 1:] if first >= 0 else ""
    return first >= 0 and "[Reader] Page drawn" in rest and "FULL" not in rest

def short_book_last(sd):
    # The one-page TXT book is the book on the card's home screen.
    json.dump({"filePath": "/books/Тестовая книга.txt", "title": "Тестовая книга", "author": "",
               "coverPath": "", "progress": 0, "page": 1, "totalPages": 1},
              open(os.path.join(sd, "books", "last_book.json"), "w"), ensure_ascii=False)

def upside_down(sd):
    merge_config(sd, ui_orientation=2)

def updown_pages(sd):
    merge_config(sd, updown_pages=True)

def config_next(sd):
    c = cfg(sd); c.update(frontlight=30, language="es", line_spacing=150)
    json.dump(c, open(os.path.join(sd, "config_next.json"), "w"), ensure_ascii=False)

def fake_server(state):
    return lambda sd: server(sd, state)

# ---------------------------------------------------------------- checks
def idle_save(log, sd):
    rec = [b for b in json.load(open(os.path.join(sd, "books/progress.json")))["books"]]
    return "Position saved while idle (5 page turns)" in log and any(b["local"].get("pg", 1) > 1 for b in rec)

def sorted_rows(log, sd):
    seen = []
    for _, _, title, _ in library_rows(log):
        if title not in seen: seen.append(title)
    return seen == SORTED

def delete_follows(log, sd):
    rows = library_rows(after(log, "Deleting downloaded book file"))
    return (not os.path.exists(os.path.join(sd, "books/Alice in Wonderland.epub"))
            and rows and rows[-1][2] == "Alice in Wonderland" and rows[-1][3] == "cloud")

def held_to_end(log, sd):
    rows = library_rows(after(log, "HOLD D"))
    return rows and rows[-1][0] == rows[-1][1] and len(rows) < 7   # at the last row, faster than a row a step

def toc_pages(log, sd):
    m = re.findall(r"drawTocScreen finished in \d+ ms \(row (\d+)/(\d+), (\d+) a page\)", log)
    if len(m) < 3: return False
    first, right, left = (int(x[0]) for x in m[-3:])
    per = int(m[-1][2])
    # Rows count from 1; the contents open on the current chapter, the first row before it.
    return first >= 1 and right == ((first - 1) // per + 1) * per + 1 and left == right - per

def jump_then_close(log, sd):
    # B6: BACK after a chapter jump closes the book at once; the jump was saved.
    p = pages(log)
    before_back = log[:log.find(">>> BACK")]
    return (len(p) >= 3 and p[-1] != p[-2] and "Reader] Page" not in after(log, ">>> BACK")
            and "drawHomeScreen" in after(log, ">>> BACK") and "Last read book saved" in after(before_back, ">>> DOWN"))

def updown_turns(log, sd):
    p = pages(after(log, ">>> DOWN"))
    p_all = pages(log)
    return len(p_all) >= 3 and p_all[-1] == p_all[-3] + 2 and p_all[-2] == p_all[-3] + 1

def language_direct(log, sd):
    tail = after(log, ">>> CONFIRM")
    return cfg(sd)["language"] == "ru" and "drawSettingsScreen" in tail and "wait" not in tail

def reload_applies(log, sd):
    tail = after(log, "CONFIG ON THE CARD REPLACED")
    return "[Light] brightness 30%" in tail and "Config successfully loaded" in tail

def saved_file(log, sd):
    return "Config saved to MicroSD" in after(log, ">>> CONFIRM")

def search_cyrillic(log, sd):
    return "[Library] Search 'тес'" in log and "List @search: 2 rows" in log

def search_backspace(log, sd):
    return "[Library] Search 'т'" in log

def subfolder_cache(log, sd):
    return (os.path.isdir(os.path.join(sd, "cache/books#Sub#Alice in Wonderland.epub.fib"))
            and "Opening book: /books/Sub/Alice in Wonderland.epub" in log)

def downloaded_no_part(log, sd):
    return (os.path.exists(os.path.join(sd, "books/The Time Machine [2].epub"))
            and "downloaded successfully" in log and not files(sd, "**/*.part"))

def failed_no_part(log, sd):
    return not files(sd, "**/*.part") and not os.path.exists(os.path.join(sd, "books/The Time Machine [2].epub"))

def token_saved(log, sd):
    return "refreshing token" in log and cfg(sd).get("token") not in ("", "tok-0")

def menu_fast(log, sd):
    tail = after(log, ">>> CONFIRM")
    return "drawReaderMenuScreen" in tail and "FAST_REFRESH" in tail.split("drawReaderMenuScreen")[0]

def first_page_fast(log, sd):
    tail = after(log, "Opening book")
    return "[EPD] Updating display (FAST_REFRESH)" in tail.split("Page drawn")[1][:400]

def preview_once(log, sd):
    tail = after(log, "drawReaderMenuScreen")
    laid = re.findall(r"Appearance preview laid out", tail)
    return laid and "is not enough" not in tail and "(cut kept)" in tail and len(laid) == tail.count(">>> ") 

CASES = [
    # name, setup(sd) or None, environment, script, check(log, sd)
    ("reader: page turns are saved while the reader is idle (B2)", None, {},
     OPEN_BOOK + "RRRRR" + "ww", idle_save),   # the idle window opens after 2.5 s
    ("power: a critical battery saves the place and sleeps (B2)", None,
     {"SIM_BATTERY_PCT": "2", "SIM_BATTERY_CHECK_MS": "300"}, OPEN_BOOK + "RR" + "w",
     lambda log, sd: "Battery critical" in log and "Entering Deep Sleep" in log.split("Battery critical")[1]),
    ("power: a critical battery sleeps without syncing, the progress stays queued (B10)",
     lambda sd: merge_config(sd, sync_on_sleep=True),
     {"SIM_BATTERY_PCT": "2", "SIM_BATTERY_CHECK_MS": "300"}, OPEN_BOOK + "RR" + "w",
     lambda log, sd: "Battery critical" in log and "no sync before sleep" in log
                     and "syncOnSleep enabled" not in log and "[WiFi] Connect" not in log
                     and "Entering Deep Sleep" in log),
    ("power: on USB a low battery does not send it to sleep (B3)", None,
     {"SIM_BATTERY_PCT": "2", "SIM_BATTERY_CHECK_MS": "300", "SIM_USB": "1"}, OPEN_BOOK + "w",
     lambda log, sd: "Battery critical" not in log and "Entering Deep Sleep" not in log),
    ("power: a failed gauge read (0 %, 0 V) is not a flat battery (B2)", None,
     {"SIM_BATTERY_PCT": "0", "SIM_BATTERY_VOLTS": "0", "SIM_BATTERY_CHECK_MS": "300"}, OPEN_BOOK + "w",
     lambda log, sd: "Battery critical" not in log),
    ("storage: a catalog.json over 50 KB is read whole (B1)", big_catalog, {}, ALL_BOOKS,
     lambda log, sd: "Loaded 400 books metadata" in log and re.search(r"Master list: 40\d books", log)),
    ("storage: a corrupt progress.json is kept as .bad (B1)", corrupt_progress, {}, OPEN_BOOK + "B",
     lambda log, sd: os.path.exists(os.path.join(sd, "books/progress.json.bad"))
                     and json.load(open(os.path.join(sd, "books/progress.json")))["books"]),
    ("library: all books by title, the ones on the card first (U6)", None, {}, ALL_BOOKS + "DDDDDD", sorted_rows),
    ("library: a deleted book stays selected on its new row (U6)", None, {},
     ALL_BOOKS + "D" + "L" + "DD" + "C" + "C" + "w", delete_follows),
    ("library: a held DOWN goes faster and stops at the end (U1)", None, {}, ALL_BOOKS + "HD", held_to_end),
    ("contents: RIGHT a page on, LEFT a page back (U1)", None, {},
     OPEN_BOOK + READER_MENU + "C" + "R" + "L", toc_pages),
    ("reader: BACK after a chapter jump closes the book, the jump is saved (B6)", None, {},
     OPEN_BOOK + "R" + "D" + "B", jump_then_close),
    ("reader: past the last page, 'end of book'; BACK stays on the page (U2)", short_book_last, {},
     OPEN_BOOK + "R" + "B",
     lambda log, sd: "[Reader] End of the book" in log and "Page 1/1" in after(log, ">>> BACK")
                     and "drawHomeScreen" not in after(log, ">>> BACK")),
    ("reader: past the last page, 'end of book'; OK closes the book (U2)", short_book_last, {},
     OPEN_BOOK + "R" + "C",
     lambda log, sd: "[Reader] End of the book" in log and "drawHomeScreen" in after(log, ">>> CONFIRM")
                     and log.count("drawHomeScreen finished") == 2),
    # B9: a book opened while the upload of the one just closed is still connecting.
    ("reader: opening a book cancels the background upload and turns Wi-Fi off (B9)",
     lambda sd: server(sd, {"wifiConnectMs": 8000}), {}, OPEN_BOOK + "RRR" + "B" + "C",
     lambda log, sd: "Background upload queued" in log
                     and "Background upload cancelled" in after(log, ">>> CONFIRM")
                     and "Radio off: the book needs the memory" in after(log, ">>> CONFIRM")),
    ("reader: UP/DOWN turn pages when set so (U4)", updown_pages, {}, OPEN_BOOK + "DD", updown_turns),
    ("appearance: the Up/Down keys row switches to pages (U4)", None, {},
     OPEN_BOOK + APPEARANCE + "UU" + "R" + "B",
     lambda log, sd: cfg(sd).get("updown_pages") is True),
    ("appearance: the preview is laid out once a redraw (P3)", None, {},
     OPEN_BOOK + APPEARANCE + "DDDR", preview_once),
    ("reader: the menu and the first page use the fast refresh (P2)", None, {},
     OPEN_BOOK + READER_MENU, lambda log, sd: menu_fast(log, sd) and first_page_fast(log, sd)),
    ("settings: a language takes effect without a 'Saved' dialog (U5)", None, {},
     SETTINGS + "C" + "D" + "C", language_direct),
    ("settings file: reload applies everything in config.json (U7, B12)", config_next, {},
     SETTINGS_FILE + "X" + "C" + "w", reload_applies),
    ("settings file: save writes config.json (U7)", None, {}, SETTINGS_FILE + "D" + "C", saved_file),
    ("keyboard: search typed in Cyrillic (U2)", russian, {},
     "DC" + "[" + kb_tap(215, 385) + kb_tap(177, 265) + kb_tap(100, 385) + kb_tap(430, 448) + "]" + "C", search_cyrillic),
    ("keyboard: backspace removes a whole Cyrillic letter (U2)", russian, {},
     "DC" + "[" + kb_tap(215, 385) + kb_tap(177, 265) + kb_tap(278, 448) + kb_tap(430, 448) + "]" + "C", search_backspace),
    # Upside down the front keys are turned for the keyboard's own input loop too
    # (C7). A letter is typed (the first nested OK is taken by the key that opened
    # the keyboard, hence two), DOWN x3 to Shift, LEFT wraps to OK; upside down the
    # physical RIGHT does that.
    ("keyboard upside down: LEFT and RIGHT follow the screen (C7)", upside_down, {},
     "DC" + "[" + "DU" + "CC" + "DDD" + "R" + "C" + "]" + "C",
     lambda log, sd: re.search(r"\[Library\] Search '.+'", log)),
    ("keyboard the right way up: LEFT from Shift wraps to OK (C7)", None, {},
     "DC" + "[" + "DU" + "CC" + "DDD" + "L" + "C" + "]" + "C",
     lambda log, sd: re.search(r"\[Library\] Search '.+'", log)),
    # U4: Shift is for one letter: Shift, a letter, a letter -> "Xx" (the first
    # nested OK is taken by the key that opened the keyboard, hence the extra C).
    ("keyboard: Shift is for one letter (U4)", None, {},
     "DC" + "[" + "DU" + "C" + "DDD" + "C" + "UUU" + "C" + "C" + "DDD" + "L" + "C" + "]" + "C",
     lambda log, sd: re.search(r"\[Library\] Search '[A-Z][a-z]'", log)),
    ("keyboard: BACK held leaves without saving (U4)", None, {},
     "DC" + "[" + "DU" + "CC" + "h" + "]" + "w",
     lambda log, sd: "[Keyboard] BACK held: left without saving" in log and "[Library] Search" not in log),
    ("home: LEFT on 'Sync now' goes back to the book card, no sync (U7)", None, {},
     "U" + "L" + "w",
     lambda log, sd: "[Sync]" not in log and "Connecting" not in log and "drawHomeScreen" in after(log, ">>> LEFT")),
    # P3: the first entry into the catalog walks the card, the second trusts the list.
    ("catalog: entered again, the card is not walked again (P3)", None, {},
     "DC" + "B" + "C",
     lambda log, sd: log.count("Card unchanged since the last walk") == 1),
    ("library: a book in a subfolder gets a cache of its own (B13)", subfolder_book, {},
     "DC" + "DDDDD" + "C" + "D" + "C" + "D" + "C", subfolder_cache),
    ("names: two server books of one title get a file each; the old file goes to the one read here",
     same_titles, {}, "w", same_titles_split),
    ("storage: a save cut before its rename is taken from the .tmp", progress_only_tmp, {}, "w", tmp_taken),
    ("download: the book is in place and no .part is left (B5)", fake_server({"progress": []}), {},
     HOME_SYNC + "B" + ALL_BOOKS + "UC" + "w", downloaded_no_part),
    ("download: a failed download leaves nothing behind (B5)", fake_server({"progress": []}), {},
     HOME_SYNC + "B" + ALL_BOOKS + "US" + "C" + "w", failed_no_part),
    ("sync: a token renewed after 401 is saved (low priority)",
     lambda sd: (merge_config(sd, token="tok-0"),
                 server(sd, {"percent": 0.3, "updatedAt": "NOW-100", "expireToken": True})), {},
     OPEN_BOOK + READER_MENU + "DDDC" + "w", token_saved),
    # B5: the clock's minute redraw is not a page. A full refresh every 2 pages;
    # the book opens (page 1) and the minute changes during the wait.
    ("status bar: the clock's minute redraw does not count towards the full refresh (B5)",
     lambda sd: merge_config(sd, sb_clock=True, full_refresh_every=2), {"SIM_RTC_UTC": "1794391198"},
     OPEN_BOOK + "ww", clock_redraw_fast),
    ("status bar: clock and UTC+3 saved and active from RTC",
     None, {"SIM_RTC_UTC": "1794391200"},
     OPEN_BOOK + APPEARANCE + "U" + "C" + "UU" + "C" + "D" + "RRRRRR" + "BB",
     lambda log, sd: "[RTC] Ready" in log and "[Clock] Restored from RTC" in log
                     and cfg(sd).get("sb_clock") is True and cfg(sd).get("utc_offset_min") == 180
                     and "Page drawn" in after(log, ">>> BACK")),
    ("imu: auto-rotate tilts on sustained gravity and sleeps when disabled",
     None if (any(d in BIN for d in ("build-x3", "build-x4c")) or os.environ.get("DEVICE") in ("x3", "x4c"))
     else (lambda sd: merge_config(sd, auto_rotate=True)),
     {"SIM_IMU_PRESENT": "1"},
     "A-1.0,0.0,0.0;w" + SETTINGS + ("UUC" if ("build-x4c" in BIN or os.environ.get("DEVICE") == "x4c") else "UC"),
     lambda log, sd: "[IMU] Ready" in log and "[App] Auto-rotated UI 0 -> 1" in log
                     and "[IMU] Put to sleep" in log and cfg(sd).get("auto_rotate") is False),
]

def run_case(case):
    name, setup, env, script, check = case
    sd = tempfile.mkdtemp(); shutil.rmtree(sd)
    shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd)
    shutil.copy(os.path.join(ROOT, "test/sim/config.json"), os.path.join(sd, "config.json"))
    for f in ("server.json", "server_next.json"):
        if os.path.exists(os.path.join(sd, f)): os.remove(os.path.join(sd, f))
    if name.startswith("download: a failed"):   # 'S' makes the server fail after the sync
        json.dump({"fail": True}, open(os.path.join(sd, "server_next.json"), "w"))
    frames = tempfile.mkdtemp()
    problems = []
    try:
        if setup: setup(sd)
        res = subprocess.run([BIN, frames, sd, script], capture_output=True, text=True, timeout=240,
                             env=dict(os.environ, **env))
        log = res.stdout
        if "STUCK" in log: problems.append("stuck in a screen's input loop")
        if res.returncode != 0: problems.append(f"exit code {res.returncode}")
        try:
            if not check(log, sd): problems.append("expected effect not seen")
        except Exception as e:
            problems.append(f"check raised {e!r}")
    except subprocess.TimeoutExpired:
        log = ""; problems.append("timeout")
    finally:
        shutil.rmtree(frames, ignore_errors=True)
        shutil.rmtree(sd, ignore_errors=True)
    return name, problems, log

def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    cases = [c for c in CASES if only in c[0]]
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(run_case, cases))
    failed = 0
    for name, problems, log in results:
        print(("ok    " if not problems else "FAIL  ") + name + ("" if not problems else "  -> " + "; ".join(problems)))
        if problems:
            failed += 1
            if os.environ.get("VERBOSE"): print(log[-4000:])
    print(f"{'OK' if not failed else 'FAILED'}: {len(cases) - failed}/{len(cases)} X4 cases passed")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
