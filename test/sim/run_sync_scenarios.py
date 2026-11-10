#!/usr/bin/env python3
"""End-to-end reading-progress sync scenarios on the headless simulator.

Each scenario starts from a copy of simulator/sdcard (the copy is thrown away),
drives the firmware with button presses against test/sim/fake_bookorbit.cpp and checks
  * the sequence of sync decisions logged for the book ("pull", "push", "none"),
  * the final position stored in /books/progress.json.
Run from the firmware/ folder:  make -f test/sim/Makefile test
"""
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.environ.get("X4_HEADLESS") or os.path.join(ROOT, "test/sim/build-x4/x4_headless")
if not os.path.isabs(BIN): BIN = os.path.join(ROOT, BIN)
SD_TEMPLATE = os.path.join(ROOT, "simulator/sdcard")
TEST_CONFIG = os.path.join(ROOT, "test/sim/config.json")   # replaces a local simulator/sdcard/config.json
BOOK = "Alice in Wonderland"
SYNC = "CDDDCw"          # in the reader: menu -> "Синхронизация" (4th) -> wait
HOME_SYNC = "UCww"       # on the home screen: "Синхронизация сейчас" -> wait

def srv(pct=None, upd="NOW-100", fail=False, **extra):
    if fail: return {"fail": True}
    d = {"percent": pct, "updatedAt": upd}
    d.update(extra)
    return d

# name, initial server, server after 'S', script, expected actions, expected final %, expected dirty
SCENARIOS = [
    ("pull, server moved forward, pull again (bug report)",
     srv(0.30), srv(0.60, "NOW"), "C" + SYNC + "S" + SYNC, ["pull", "pull"], 60.0, False),
    ("server moved BACKWARD is applied",
     srv(0.60), srv(0.10, "NOW"), "C" + SYNC + "S" + SYNC, ["pull", "pull"], 10.0, False),
    ("read here, server changed LATER -> server wins",
     srv(0.30), srv(0.10, "NOW+30"), "C" + SYNC + "RRS" + SYNC, ["pull", "pull"], 10.0, False),
    ("server changed an hour ago, read here now -> device wins",
     srv(0.30, "NOW-7200"), srv(0.10, "NOW-3600"), "C" + SYNC + "RRS" + SYNC, ["pull", "push"], 31.0, False),
    ("web reader only bumped updatedAt -> nothing",
     srv(0.30), srv(0.301, "NOW+30"), "C" + SYNC + "S" + SYNC, ["pull", "none"], 30.0, False),
    ("read here, server unchanged -> push, then in sync",
     srv(0.30), None, "C" + SYNC + "RRR" + SYNC + SYNC, ["pull", "push", "none"], 31.7, False),
    ("closing the book uploads unsynced reading",
     srv(0.30), None, "C" + SYNC + "RRRB", ["pull", "push"], 31.7, False),
    ("home-screen sync with the book closed, then open",
     srv(0.30), srv(0.60, "NOW"), HOME_SYNC + "S" + "Cww" + "BC", ["pull", "pull"], 60.0, False),
    ("server unreachable: reading stays queued",
     srv(fail=True), None, "CRRRB", [], None, True),
    # Device log 2026-11-07: ~12 KB free heap during sync cut the catalog after the first
    # book; the book being read was not in it and its progress was never synced.
    ("low memory truncates the catalog: the open book still syncs",
     srv(0.30, deviceHeap=5000), srv(0.60, "NOW", deviceHeap=5000), "C" + SYNC + "S" + SYNC, ["pull", "pull"], 60.0, False),
    ("low memory, no saved catalog: a known book still syncs",
     srv(0.30, deviceHeap=5000, _noSavedCatalog=True, _seedRecord=True), srv(0.60, "NOW", deviceHeap=5000), "C" + SYNC + "S" + SYNC, ["pull", "pull"], 60.0, False),
    # The user's flow: sync, change on the server, sleep + wake, sync again.
    ("sleep and wake between two syncs",
     srv(0.30), srv(0.60, "NOW"), "C" + SYNC + "SZ" + "C" + SYNC, ["pull", "pull"], 60.0, False),
    ("upload rejected by the server: reading stays queued",
     srv(0.30), srv(0.30, failPut=True), "C" + SYNC + "RRRS" + SYNC, ["pull", "push"], 31.7, True),
    ("server has no record: local reading is uploaded",
     {}, None, "CRRR" + SYNC, ["push"], None, False),
    # One book, several formats: each has its own position. The FB2 read on another
    # device later does not move the EPUB open here.
    ("another format's newer position does not move this one",
     {"progress": [{"fileId": 101, "percentage": 30, "updatedAt": "NOW-100"},
                   {"fileId": 111, "percentage": 80, "updatedAt": "NOW-10"}]},
     None, "C" + SYNC, ["pull"], 30.0, False),
]

def run(sd, script):
    frames = tempfile.mkdtemp()
    try:
        out = subprocess.run([BIN, frames, sd, script], capture_output=True, text=True, timeout=300).stdout
    finally:
        shutil.rmtree(frames, ignore_errors=True)
    return re.findall(r"\[Sync\] '" + BOOK + r"': .*-> (pull|push|none):", out), out

def stored(sd):
    try:
        books = json.load(open(os.path.join(sd, "books/progress.json")))["books"]
    except FileNotFoundError:
        return None
    for b in books:
        if b["title"] == BOOK:
            return b
    return None

def prepare(first, nxt):
    """Fresh SD card copy with the given server state. Keys starting with '_' set up the device:
    _noSavedCatalog, _seedRecord, _config {...} (merged into config.json), _progressJson "raw",
    _lastBook "<title>" (book on the home screen, default BOOK)."""
    sd = tempfile.mkdtemp()
    shutil.rmtree(sd); shutil.copytree(SD_TEMPLATE, sd)
    shutil.copy(TEST_CONFIG, os.path.join(sd, "config.json"))
    for f in ("books/progress.json", "server.json", "server_next.json"):
        p = os.path.join(sd, f)
        if os.path.exists(p): os.remove(p)
    first = dict(first)
    last = first.pop("_lastBook", BOOK + ".epub")
    if last is None:
        os.remove(os.path.join(sd, "books/last_book.json"))
    else:
      with open(os.path.join(sd, "books/last_book.json"), "w") as f:
        json.dump({"filePath": "/books/" + last, "title": os.path.splitext(last)[0], "progress": 0, "page": 1,
                   "totalPages": 1, "fileId": first.pop("_fileId", 101 if last == BOOK + ".epub" else 0)},
                  f, ensure_ascii=False)
    if first.pop("_noSavedCatalog", False):
        os.remove(os.path.join(sd, "books/catalog.json"))
    if first.pop("_seedRecord", False):   # downloaded from the catalog before: ids known
        with open(os.path.join(sd, "books/progress.json"), "w") as f:
            json.dump({"version": 1, "books": [{"fileId": 101, "bookId": 1, "path": "/books/" + BOOK + ".epub",
                                                 "title": BOOK, "local": {"p": 0}}]}, f, ensure_ascii=False)
    raw = first.pop("_progressJson", None)
    if raw is not None:
        with open(os.path.join(sd, "books/progress.json"), "w") as f: f.write(raw)
    cfg_extra = first.pop("_config", None)
    if cfg_extra:
        cp = os.path.join(sd, "config.json")
        cfg = json.load(open(cp)); cfg.update(cfg_extra)
        with open(cp, "w") as f: json.dump(cfg, f, ensure_ascii=False)
    with open(os.path.join(sd, "server.json"), "w") as f: json.dump(first, f)
    if nxt is not None:
        with open(os.path.join(sd, "server_next.json"), "w") as f: json.dump(nxt, f)
    return sd

def run_one(scenario):
    name, first, nxt, script, want_actions, want_pct, want_dirty = scenario
    sd = prepare(first, nxt)
    actions, log = run(sd, script)
    rec = stored(sd)
    problems = []
    if actions != want_actions:
        problems.append("actions %s, expected %s" % (actions, want_actions))
    if rec is None:
        problems.append("no progress record")
    else:
        pct = rec["local"]["p"] * 100
        # Positions land on page starts and a page is ~0.75% of the sample book: within one page.
        if want_pct is not None and abs(pct - want_pct) > 0.8:
            problems.append("final %.1f%%, expected %.1f%%" % (pct, want_pct))
        if bool(rec.get("dirty", False)) != want_dirty:
            problems.append("dirty=%s, expected %s" % (rec.get("dirty", False), want_dirty))
    shutil.rmtree(sd, ignore_errors=True)
    if problems:
        return False, "FAIL  " + name + ": " + "; ".join(problems)
    return True, "ok    " + name + "  " + " ".join(actions)

CLOUD = "B" + "DC" + "DC" + "UC"  # after a home sync: Home -> Catalog -> All Books -> wrap up to cloud book ("Moby Dick")
# After a home sync: Home -> Catalog -> All Books -> "Alice in Wonderland" -> book menu -> Formats.
# The fake server has Alice as EPUB (101), TXT (131), AZW3 (121) and FB2 (111), in that order.
FORMATS = "B" + "DC" + "DC" + "D" + "L" + "DC"
P40 = [{"fileId": f, "percentage": 40, "updatedAt": "NOW-50"} for f in (101, 102, 103)]

# name, server state, server after 'S', script, log must match (all), log must not match (any)
LOG_SCENARIOS = [
    ("download a cloud book: file, cover, server position",
     {"progress": P40}, None, HOME_SYNC + CLOUD + "w",
     [r"\[Sync\] Book /books/.+ downloaded successfully", r"Download finished: \d+ bytes .* result \d+",
      r"\[Sync\] '(The Time Machine|Moby Dick)': local 0\.0% .* server 40\.0% .* -> pull"], [r"Incomplete download"]),
    ("download cancelled with BACK",
     {"backOn": "/download"}, None, HOME_SYNC + "S" + CLOUD + "w",
     [r"\(cancelled\)", r"Incomplete download removed", r"Operation aborted by user"], [r"downloaded successfully"]),
    ("download: no Wi-Fi",
     {}, {"wifiDown": True}, HOME_SYNC + "S" + CLOUD + "w",
     [r"Connection timeout"], [r"downloaded successfully", r"GET /api/v1/opds/\d+/download"]),
    ("download: BACK while Wi-Fi connects",
     {}, {"backOnWifi": True}, HOME_SYNC + "S" + CLOUD + "w",
     [r"Operation aborted by user"], [r"GET /api/v1/opds/\d+/download"]),
    ("download fails: server error",
     {}, {"fail": True}, HOME_SYNC + "S" + CLOUD + "w",
     [r"Download failed with HTTP 500"], [r"downloaded successfully"]),
    ("full sync: Wi-Fi down",
     {"wifiDown": True}, None, HOME_SYNC,
     [r"Wi-Fi connection failed"], [r"GET /api/v1/opds/catalog"]),
    ("full sync: BACK while Wi-Fi connects (no network)",
     {"wifiDown": True, "backOnWifi": True}, None, HOME_SYNC,
     [r"Connection aborted by user", r"Operation aborted by user"], [r"GET /api/v1/opds/catalog"]),
    ("full sync: BACK right after Wi-Fi connected",
     {"backOnWifi": True}, None, HOME_SYNC,
     [r"Operation aborted by user"], [r"GET /api/v1/opds/catalog"]),
    ("full sync: BACK during the progress requests",
     {"backOn": "/api/v1/books/1/progress", "progress": P40}, None, "C" + SYNC,
     [r"Operation aborted by user"], [r"GET /api/v1/opds/catalog"]),
    ("full sync: BACK during the catalog",
     {"backOn": "/api/v1/opds/catalog"}, None, HOME_SYNC,
     [r"Operation aborted by user"], [r"Catalog metadata saved"]),
    ("full sync: BACK during the cover downloads",
     {"backOn": "/thumbnail"}, None, HOME_SYNC,
     [r"Operation aborted by user", r"Catalog metadata saved"], []),
    ("full sync: BACK during login",
     {"_config": {"token": ""}, "backOn": "/auth/login"}, None, HOME_SYNC,
     [r"Operation aborted by user"], [r"GET /api/v1/opds/catalog"]),
    ("full sync logs in when there is no token",
     {"_config": {"token": ""}, "progress": P40}, None, HOME_SYNC,
     [r"POST /api/v1/auth/login -> 200", r"Authentication successful"], [r"-> 401"]),
    ("full sync without a last book picks one",
     {"_lastBook": None}, None, HOME_SYNC,
     [r"Catalog metadata saved"], []),
    ("full sync: server down",
     {"fail": True}, None, HOME_SYNC,
     [r"catalog fetch failed"], [r"Catalog metadata saved"]),
    ("full sync: unusable catalog keeps the saved one",
     {"brokenCatalog": True}, None, HOME_SYNC,
     [r"Empty catalog looks wrong: http=200 err=0 bytes=\d+ skipped=3"], [r"Catalog metadata saved \(0 books\)"]),
    ("expired token is refreshed",
     {"percent": 0.3, "updatedAt": "NOW-100", "expireToken": True, "_config": {"token": "tok-0"}}, None, "C" + SYNC,
     [r"Progress fetch returned 401, refreshing token", r"-> pull"], []),
    ("expired token on upload is refreshed",
     srv(0.30, _config={"token": "tok-0"}), {"percent": 0.3, "updatedAt": "NOW-100", "expireToken": "PUT"},
     "C" + SYNC + "RRRS" + "B",
     [r"updateBookProgress returned 401, refreshing token", r"-> push"], []),
    ("upload sends the CFI (web reader position), nothing to KOSync",
     srv(0.30), None, "C" + SYNC + "RRR" + SYNC,
     [r"-> push", r'updateBookProgress \{"writes":\[\{"fileId":101,"percentage":[\d.]+,"text":\{"cfi":"epubcfi\(/6/\d+!(/\d+)+(:\d+)?\)"\}\}\]\} -> HTTP 200'],
     [r'"koreader":', r"FakeHttp\] \w+ /api/v1/koreader"]),
    # A position in the Book Orbit web reader CFI format: the start of "Chapter 10" (spine item 17).
    ("web reader CFI opens the exact chapter start",
     {"_lastBook": "01. Frankenstein (1818).epub", "_fileId": 101,
      "progress": [{"fileId": 101, "percentage": 37.2, "cfi": "epubcfi(/6/34!/4/2[pgepubid00016]/2/3:0)", "updatedAt": "NOW-50"}]},
     None, "C" + SYNC,
     [r"-> pull", r"\[CFI\] 'epubcfi\(/6/34!/4/2\[pgepubid00016\]/2/3:0\)' -> spine 16, char \d+"], [r"stale"]),
    # The web reader's percent differs from ours by a few percent in short illustrated
    # books ("Alice": 3.3% on the X4): the CFI still wins.
    ("web reader CFI wins over a percent a few percent off",
     {"_lastBook": "01. Frankenstein (1818).epub", "_fileId": 101,
      "progress": [{"fileId": 101, "percentage": 32, "cfi": "epubcfi(/6/34!/4/2[pgepubid00016]/2/3:0)", "updatedAt": "NOW-50"}]},
     None, "C" + SYNC,
     [r"-> pull", r"\[CFI\] 'epubcfi\(/6/34!/4/2\[pgepubid00016\]/2/3:0\)' -> spine 16, char \d+",
      r"\[Mem\] engine probe book opened: layout 50 KB ok, parse 46 KB ok, book 30/30 KB.*FITS"],
     [r"stale"]),
    ("stale CFI (another reader moved on) falls back to the percentage",
     {"_lastBook": "01. Frankenstein (1818).epub", "_fileId": 101,
      "progress": [{"fileId": 101, "percentage": 60, "cfi": "epubcfi(/6/34!/4/2[pgepubid00016]/2/3:0)", "updatedAt": "NOW-50"}]},
     None, "C" + SYNC,
     [r"-> pull", r"stale, not used"], []),
    ("no Wi-Fi configured",
     {"_config": {"wifi_ssid": ""}}, None, HOME_SYNC,
     [r"Not configured: Wi-Fi"], [r"\[WiFi\] Initializing"]),
    ("no server configured",
     {"_config": {"server_url": ""}}, None, HOME_SYNC,
     [r"Not configured: server"], [r"\[WiFi\] Initializing"]),
    ("sync on boot pulls the last book",
     {"percent": 0.3, "updatedAt": "NOW-100", "_config": {"sync_on_boot": True}}, None, "w",
     [r"\[Boot\] syncOnBoot enabled", r"-> pull"], []),
    ("sync on boot: Wi-Fi down",
     {"wifiDown": True, "_config": {"sync_on_boot": True}}, None, "w",
     [r"\[Boot\] syncOnBoot enabled"], [r"\[Sync\] '"]),
    ("reading queued offline is uploaded before sleep",
     {"wifiDown": True, "_config": {"sync_on_sleep": True}}, srv(0.0, "NOW-9000"), "CRRRB" + "S" + "B",
     [r"progress stays queued", r"syncOnSleep enabled", r"-> push"], []),
    ("corrupt progress.json is ignored",
     {"_progressJson": "{not json"}, None, "w",
     [r"progress.json is corrupt"], []),
    ("catalog: one book in four formats, EPUB preferred over the last link",
     {}, None, HOME_SYNC,
     [r"Found book: Alice in Wonderland \(id=1, fileId=101, 4 formats", r"Catalog metadata saved \(3 books\)"],
     [r"format FB2", r"download\?fileId=111"]),
    ("saved catalog of older firmware: its address still downloads",
     {}, None, "DC" + "DC" + "UC" + "w",
     [r"GET /api/v1/download/103 -> 200", r"Book /books/Moby Dick.epub downloaded successfully"], []),
    # Every format the server lists is readable now (FB2, MOBI and AZW3 through a
    # conversion): choosing one downloads it. The fake file is no real AZW3: the
    # conversion refuses it and the book's format stays.
    ("formats list: an AZW3 is downloaded and converted; a file that is no book is refused",
     {}, None, HOME_SYNC + FORMATS + "DDC" + "w",
     [r"Formats of 'Alice in Wonderland': 4", r"download\?fileId=121", r"\[Convert\] .*Alice in Wonderland\.azw3"],
     [r"cannot be opened yet"]),
    ("formats list: another format is downloaded and opened at its own position",
     {"progress": [{"fileId": 101, "percentage": 40, "updatedAt": "NOW-50"},
                   {"fileId": 131, "percentage": 10, "updatedAt": "NOW-50"}]}, None, HOME_SYNC + FORMATS + "DC" + "w",
     [r"'Alice in Wonderland': format TXT \(/books/Alice in Wonderland.txt\)",
      r"GET /api/v1/opds/1/download\?fileId=131 -> 200",
      r"Book /books/Alice in Wonderland.txt downloaded successfully",
      r"\[Sync\] 'Alice in Wonderland': local 0\.0% .* server 10\.0% .* -> pull",
      r"Restoring position: .*10\.0%"],
     [r"server 40\.0% .* -> pull.*\n.*Alice in Wonderland.txt"]),
    ("the format read last opens from the list; BACK leaves the formats list",
     {}, None, HOME_SYNC + FORMATS + "DCw" + "B" + "C" + "DC" + "D" + "C" + "w" + "B" + "C" + "DC" + "D" + "LDC" + "B",
     [r"Book /books/Alice in Wonderland.txt downloaded successfully",
      r"Formats of 'Alice in Wonderland': 4(.|\n)*Formats of 'Alice in Wonderland': 4",
      r"Opening book: /books/Alice in Wonderland.txt(.|\n)*Opening book: /books/Alice in Wonderland.txt",
      r"Formats of 'Alice in Wonderland': 4(.|\n)*List /books: 7 rows"], []),
    # Book menu -> "Sync" (Read, Formats, Sync): only that book, only its active format.
    ("book menu: sync one book",
     {"progress": P40}, None, HOME_SYNC + "B" + "DC" + "DC" + "D" + "L" + "DDC",
     [r"GET /api/v1/books/1/progress", r"Book menu sync of 'Alice in Wonderland': result 0"], []),
    ("book menu: sync fails when the server is down",
     {"progress": P40}, {"fail": True}, HOME_SYNC + "S" + "B" + "DC" + "DC" + "D" + "L" + "DDC",
     [r"Book menu sync of 'Alice in Wonderland': result 1"], []),
    ("book menu: sync without Wi-Fi settings",
     {"_config": {"wifi_ssid": ""}}, None, "DC" + "DC" + "D" + "L" + "DC",   # one format: no "Formats" row
     [r"Book menu sync of 'Alice in Wonderland': result 2"], [r"\[WiFi\] Initializing"]),
    # A record from before the book had server ids, and no saved catalog: the ids
    # come from the fresh catalog, by the path the EPUB is downloaded to.
    ("record without server ids gets them from the fresh catalog",
     {"_noSavedCatalog": True, "progress": P40,
      "_progressJson": json.dumps({"version": 1, "books": [{"path": "/books/" + BOOK + ".epub", "title": BOOK,
                                                            "local": {"p": 0}}]})}, None, HOME_SYNC,
     [r"\[Sync\] '" + BOOK + r"': local 0\.0% .* server 40\.0% .* -> pull"], []),
    ("book without a server id is not synced by file id",
     {"_lastBook": "Frankenstein.epub", "progress": P40}, None, "CRB",
     [r"book id unknown yet"], [r"GET /api/v1/books/101/progress"]),
]

def run_log(scenario):
    name, first, nxt, script, must, must_not = scenario
    sd = prepare(first, nxt)
    _, log = run(sd, script)
    shutil.rmtree(sd, ignore_errors=True)
    problems = ["missing /%s/" % m for m in must if not re.search(m, log)]
    problems += ["unexpected /%s/" % m for m in must_not if re.search(m, log)]
    if problems:
        return False, "FAIL  " + name + ": " + "; ".join(problems)
    return True, "ok    " + name

# Bookmarks synced with the web reader's: (name, server state, bookmarks file on the
# card before (None: none), script, check(server bookmarks, local file dict, log)).
MARK7 = {"id": 7, "bookId": 1, "fileId": 101, "cfi": "epubcfi(/6/8!/4/2/1:0)", "title": "Down the Rabbit-Hole"}
LOCAL7 = {"spine": 3, "chars": 0, "percent": 0.09, "chapter": "Down the Rabbit-Hole", "snippet": "",
          "cfi": "epubcfi(/6/8!/4/2/1:0)", "sid": 7}
OPEN_MARKS = "CDDwC"   # in the reader: menu -> "Закладки"
BOOKMARK_SCENARIOS = [
    ("bookmark made here goes up with its CFI and chapter",
     {"progress": []}, None, "CwRRw" + OPEN_MARKS + "wC" + SYNC + "ww",
     lambda srv, loc, log: len(srv) == 1 and srv[0]["cfi"].startswith("epubcfi(/6/4!") and srv[0]["fileId"] == 101
     and srv[0]["title"] and loc["bookmarks"][0].get("sid") == srv[0]["id"]),
    ("bookmark made in the web reader comes down, is placed and opens its page",
     {"progress": [], "bookmarks": [MARK7]}, None, "Cw" + SYNC + "ww" + OPEN_MARKS + "DC",
     lambda srv, loc, log: loc["bookmarks"][0]["sid"] == 7 and loc["bookmarks"][0]["spine"] == 3
     and re.findall(r"\[Reader\] Page \d+/\d+ \(chapter (\d+)", log)[-1] == "3"),
    ("bookmark deleted here is deleted on the server",
     {"progress": [], "bookmarks": [MARK7]}, {"bookmarks": [LOCAL7]}, "Cw" + OPEN_MARKS + "DLBBw" + SYNC + "ww",
     lambda srv, loc, log: srv == [] and loc is None),
    ("bookmark deleted on the server goes here; an old one without a CFI gets one and goes up",
     {"progress": []}, {"bookmarks": [LOCAL7, {"spine": 1, "chars": 836, "percent": 0.02, "chapter": "Old", "snippet": "x"}]},
     "Cw" + SYNC + "ww",
     lambda srv, loc, log: [m["title"] for m in srv] == ["Old"] and [b["chapter"] for b in loc["bookmarks"]] == ["Old"]),
    ("deletes wait for the server; nothing is re-added meanwhile",
     {"progress": [], "bookmarks": [MARK7], "failDelete": True}, {"bookmarks": [LOCAL7]}, "Cw" + OPEN_MARKS + "DLBBw" + SYNC + "ww",
     lambda srv, loc, log: len(srv) == 1 and loc["bookmarks"] == [] and loc["deleted"] == [7]),
]

def run_bookmarks(scenario):
    name, server, local, script, check = scenario
    sd = prepare(dict(server), None)
    marks = os.path.join(sd, "bookmarks", BOOK + ".epub.json")
    if local is not None:
        os.makedirs(os.path.dirname(marks), exist_ok=True)
        with open(marks, "w") as f: json.dump(local, f, ensure_ascii=False)
    _, log = run(sd, script)
    srv = json.load(open(os.path.join(sd, "server.json"))).get("bookmarks", [])
    loc = json.load(open(marks)) if os.path.exists(marks) else None
    try:
        ok = check(srv, loc, log)
    except Exception as e:
        ok = False
        name += " (%r)" % e
    shutil.rmtree(sd, ignore_errors=True)
    if not ok:
        return False, "FAIL  " + name + ": server %s, card %s" % (srv, loc)
    return True, "ok    " + name

def main():
    if not os.path.exists(BIN):
        sys.exit("build first: make -f test/sim/Makefile")
    if len(sys.argv) > 2 and sys.argv[1] == "--log":   # print the firmware log of one scenario
        for sc in SCENARIOS + LOG_SCENARIOS:
            if sys.argv[2] in sc[0]:
                sd = prepare(sc[1], sc[2]); print(run(sd, sc[3])[1]); shutil.rmtree(sd, ignore_errors=True)
                return
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = (list(pool.map(run_one, SCENARIOS)) + list(pool.map(run_log, LOG_SCENARIOS)) +
                   list(pool.map(run_bookmarks, BOOKMARK_SCENARIOS)))
    failed = 0
    for ok, line in results:
        print(line)
        failed += 0 if ok else 1
    total = len(SCENARIOS) + len(LOG_SCENARIOS) + len(BOOKMARK_SCENARIOS)
    print("%s: %d/%d scenarios passed" % ("FAILED" if failed else "OK", total - failed, total))
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
