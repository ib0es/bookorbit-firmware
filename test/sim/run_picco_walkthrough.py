#!/usr/bin/env python3
"""Picco walkthrough: every function reached with touch and the UP / DOWN / POWER
keys only (the Picco build of the headless runner refuses L, R, C and B).

    make -f test/sim/Makefile DEVICE=picco all
    python3 test/sim/run_picco_walkthrough.py

Each case runs the runner on a fresh copy of simulator/sdcard and checks the
log (and the card) for what the steps must have done. Coordinates are logical
portrait pixels (480x800) taken from the screens' layout.
"""
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.environ.get("PICCO_HEADLESS") or os.path.join(ROOT, "test/sim/build-picco/x4_headless")

BACK = "W100,300,400,300;"        # swipe right = back
TITLE = "T240,20;"                # ◀ in the title bar = back
HOME_CARD = "T200,200;"
HOME_CATALOG = "T200,385;"
CATALOG_ALL_BOOKS = "T200,145;"
HOME_ALL_BOOKS = HOME_CATALOG + CATALOG_ALL_BOOKS
LIB_FRANKENSTEIN = "T200,171;"  # 3rd row of All Books (sorted by title)
HOME_SETTINGS = "T200,447;"
HOME_SYNC = "T200,515;"
READ_MIDDLE = "T240,400;"         # reading: middle third = book menu
READ_RIGHT = "T400,400;"
READ_LEFT = "T80,400;"
RMENU_TOC, RMENU_FONT, RMENU_MARKS = "T240,200;", "T240,278;", "T240,354;"
RMENU_SYNC, RMENU_RESUME, RMENU_CLOSE = "T240,430;", "T240,506;", "T240,582;"
MARKS_THIS_PAGE = "T240,90;"   # first row of the Bookmarks screen
APPEAR_TYPEFACE, APPEAR_SIZE = "T240,67;", "T240,103;"   # rows of the Appearance screen
APPEAR_ORIENTATION = "T100,463;"   # on the label: the middle of a value row is its "-" (C5)
APPEAR_STATUS_BAR = "T240,584;"   # after "Up / Down keys"
SB_BOTTOM = "T100,211;"   # the last row of the status bar menu, on its label (the middle is "-")
# Settings rows (nine of them with the frontlight and the screen orientation)
SET_LANG, SET_WIFI, SET_SERVER, SET_SLEEP, SET_INFO, SET_LIGHT = "T240,85;", "T240,150;", "T240,215;", "T240,280;", "T240,475;", "T240,605;"
SET_SYNC = "T240,540;"
SET_ORIENTATION = "T240,670;"
SET_ORIENTATION_LANDSCAPE = "T290,400;"   # the same row once the screen is turned (nine rows, two pages)

def pages(log):
    return [tuple(map(int, m)) for m in re.findall(r"\[Reader\] Page (\d+)/(\d+)", log)]

def config(sd):
    with open(os.path.join(sd, "config.json")) as f:
        return json.load(f)

CASES = [
    ("home: swipe steps through recent books, tap opens the book",
     "W300,200,60,200;" + HOME_CARD + "w",
     lambda log, sd: "SWIPE_LEFT" in log and "Frankenstein (1818)" in log.split("SWIPE_LEFT")[-1] and pages(log)),
    ("reader: tap zones, swipes and UP/DOWN keys turn pages",
     HOME_CARD + "w" + READ_RIGHT + "W400,400,80,400;" + "D" + "U" + READ_LEFT + "W80,400,400,400;",
     lambda log, sd: [p for p, _ in pages(log)][-7:] == [1, 2, 3, 4, 3, 2, 1]),
    ("reader: while the page is read, the next chapters are laid out in the background",
     HOME_CARD + "w" + "w" + "w",
     lambda log, sd: "laid out in the background: ok" in log and "gave up" not in log),
    ("reader menu: appearance, font size list, tap 20 pt, back lays the book out",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_FONT + APPEAR_SIZE + "T240,236;" + TITLE,
     lambda log, sd: re.search(r"Chapter \d+( laid out|: first page)", log.split("TAP_240_236", 1)[1]) and config(sd)["font_pt"] == 20),
    ("reader menu: appearance, typeface list, tap the built-in one, back to the page",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_FONT + APPEAR_TYPEFACE + "T240,85;" + TITLE,
     lambda log, sd: "[Reader] Page" in log.split("TAP_240_85", 1)[1]
     and config(sd).get("font_family", "") == ""),
    ("reader menu: landscape pages, the tap zones turn with them",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_FONT + APPEAR_ORIENTATION + TITLE + "w" + "T264,700;T264,700;T264,100;",
     lambda log, sd: config(sd)["orientation"] == 1 and [p for p, _ in pages(log)][-3:] == [2, 3, 2]),
    ("reader menu: status bar menu, the bottom line off gives the text its room",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_FONT + APPEAR_STATUS_BAR + SB_BOTTOM + SB_BOTTOM + TITLE + TITLE + "w",
     lambda log, sd: config(sd)["sb_bottom"] == 2 and "[Reader] Page" in log.split("TAP_100_211")[-1]),
    ("reader menu: table of contents, tap a chapter",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_TOC + "T240,300;",
     lambda log, sd: len(pages(log)) >= 2 and pages(log)[-1][0] != pages(log)[-2][0]),
    ("reader menu: bookmarks, tap 'this page' adds a bookmark shown on the page",
     HOME_CARD + "w" + READ_RIGHT + READ_MIDDLE + RMENU_MARKS + MARKS_THIS_PAGE,
     lambda log, sd: "[Bookmarks] added" in log and "[Reader] Page" in log.split("[Bookmarks] added", 1)[1]
     and os.path.isdir(os.path.join(sd, "bookmarks"))),
    ("reader menu: back by title tap, close book returns home",
     HOME_CARD + "w" + READ_MIDDLE + TITLE + READ_MIDDLE + RMENU_CLOSE,
     lambda log, sd: "drawHomeScreen" in log.split("TAP_240_582")[-1]),
    ("library: tap a book opens its menu, Read opens it, back to library by swipe",
     HOME_ALL_BOOKS + LIB_FRANKENSTEIN + "T240,180;w" + READ_MIDDLE + RMENU_CLOSE,
     lambda log, sd: "Opening book: /books/Frankenstein.epub" in log and pages(log)),
    ("library: delete a book with the dialog's Delete button",
     HOME_ALL_BOOKS + LIB_FRANKENSTEIN + "T240,256;" + "T340,475;",
     lambda log, sd: "Deleting downloaded book file: /books/Frankenstein.epub" in log
                     and not os.path.exists(os.path.join(sd, "books/Frankenstein.epub"))),
    ("library: Cancel in the delete dialog keeps the book",
     HOME_ALL_BOOKS + LIB_FRANKENSTEIN + "T240,256;" + "T140,475;",
     lambda log, sd: "Deleting" not in log and os.path.exists(os.path.join(sd, "books/Frankenstein.epub"))),
    ("catalog: a book's formats list, tap a format in the cloud downloads and opens it",
     HOME_SYNC + "ww" + HOME_CATALOG + CATALOG_ALL_BOOKS + "T200,123;" + "T240,275;" + "T200,267;" + "w",
     lambda log, sd: "Alice in Wonderland [1].fb2 downloaded successfully" in log.split("TAP_200_267", 1)[1]
     and "Opening book: /books/Alice in Wonderland [1].fb2" in log),
    ("library: book menu Back and title tap return",
     HOME_ALL_BOOKS + LIB_FRANKENSTEIN + TITLE + LIB_FRANKENSTEIN + BACK + TITLE + TITLE,
     lambda log, sd: log.rstrip().endswith("frames") and "drawHomeScreen" in log.split("TAP_240_20")[-1]),
    ("settings: language by tap",
     HOME_SETTINGS + SET_LANG + "T240,165;w",
     lambda log, sd: config(sd)["language"] in ("ru", 1, "RU")),
    ("settings: sleep cover submenu, tap 'Crop to fill'",
     HOME_SETTINGS + SET_SLEEP + "T240,165;",
     lambda log, sd: "Config saved" in log.split("TAP_240_165", 1)[-1]),
    ("settings: device info opens and a tap closes it",
     HOME_SETTINGS + SET_INFO + "T240,400;" + BACK,
     lambda log, sd: "device info" in log and "drawHomeScreen" in log.split("TAP_240_400")[-1]),
    ("settings: synchronization submenu, toggles by tap",
     HOME_SETTINGS + SET_SYNC + "T240,90;" + "T240,240;" + BACK,
     lambda log, sd: config(sd)["sync_on_close"] is False and config(sd)["sync_on_wake"] is True
                     and "drawSettingsScreen" in log.split("SWIPE_100_300_400_300", 1)[1]),
    ("power: after sleep, the book syncs in the background",
     HOME_SETTINGS + SET_SYNC + "T240,240;" + BACK + BACK + "P" + "Z" + "ww",
     lambda log, sd: "Background sync queued" in log and re.search(r"Background sync (done|failed) in", log)),
    ("settings: screen orientation turns the whole interface, a quarter turn a tap",
     HOME_SETTINGS + SET_ORIENTATION + SET_ORIENTATION_LANDSCAPE,
     lambda log, sd: config(sd)["ui_orientation"] == 2 and config(sd).get("orientation", 0) == 0
                     and "drawSettingsScreen" in log.split("TAP_290_400")[-1]),
    ("settings: frontlight + button",
     HOME_SETTINGS + SET_LIGHT + "T442,124;" + BACK,
     lambda log, sd: "[Light] brightness 10%" in log and config(sd)["frontlight"] == 10),
    ("Wi-Fi: SSID typed on the on-screen keyboard by tap",
     HOME_SETTINGS + SET_WIFI + "[T60,385;T430,448;]" + SET_WIFI + "w",
     lambda log, sd: config(sd)["wifi_ssid"].endswith("x")),
    ("Wi-Fi: keyboard left by the title (nothing saved)",
     HOME_SETTINGS + SET_WIFI + "[T60,385;T240,20;]" + SET_WIFI + "w",
     lambda log, sd: not config(sd)["wifi_ssid"].endswith("x")),
    ("Wi-Fi: network list, title tap goes back",
     HOME_SETTINGS + SET_WIFI + "[T240,20;]" + "T240,90;w",
     lambda log, sd: "Starting Wi-Fi network scan" in log),
    ("server: connection test by tap",
     HOME_SETTINGS + SET_SERVER + "T240,315;w",
     lambda log, sd: "Authenticating" in log or "Authentication" in log),
    ("home: sync now by tap",
     HOME_SYNC + "ww",
     lambda log, sd: "[Sync]" in log),
    ("home: a tap cancels a running sync",
     "[T240,400;]" + HOME_SYNC + "ww",
     lambda log, sd: "Operation aborted by user" in log),
    # Landscape: the home rows sit beside the book card (stacked they were below
    # the screen). Taps in the panel's portrait coordinates: the Settings row.
    ("home in landscape: the Settings row is on screen and opens by tap",
     "T285,620;",
     lambda log, sd: "drawSettingsScreen" in log, {"ui_orientation": 1}),
    ("home in landscape (the other way): Sync now is on screen",
     "T250,200;",
     lambda log, sd: "[Sync]" in log or "SYNC" in log or "Wi-Fi" in log, {"ui_orientation": 3}),
    # Landscape lists: cards keep their second line inside, and what does not fit
    # is a swipe away (the page swipe moves the selection to the next page).
    ("catalog in landscape: swipe up to the second page, open Downloaded",
     "T355,620;W129,400,329,400;T399,400;",
     lambda log, sd: "drawCatalogScreen" in log and "List /books: 5 rows" in log, {"ui_orientation": 1}),
    ("book menu in landscape: swipe up to the second page, Close book",
     "T229,200;wT240,400;W129,400,329,400;T254,400;",
     lambda log, sd: "[Reader] Close" in log and log.rstrip().count("drawHomeScreen") >= 2, {"ui_orientation": 1}),
    ("memory (large tier): syncing from an open book keeps the reader's layout and font caches",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_SYNC + "w",
     lambda log, sd: "[WiFi] The reader keeps its memory" in log and "Reader memory given up" not in log
     and "[Mem] sync: Wi-Fi up" in log),
    ("memory (large tier): opening a book leaves the background upload running (B9)",
     HOME_CARD + "w" + READ_RIGHT + READ_MIDDLE + RMENU_CLOSE + HOME_CARD,
     lambda log, sd: "Background upload queued" in log and "Background upload cancelled" not in log
                     and "Radio off" not in log, None, {"wifiConnectMs": 8000}),
    # C5: "-" and "+" beside a value (line spacing: the third row).
    ("appearance: '-' beside a value steps it down, '+' up (C5)",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_FONT + "T320,140;",
     lambda log, sd: config(sd).get("line_spacing", 100) < 100),
    ("appearance: '+' beside a value steps it up (C5)",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_FONT + "T432,140;",
     lambda log, sd: config(sd).get("line_spacing", 100) > 100),
    # C1: a bookmark added, then deleted by the "delete" beside it.
    ("bookmarks: 'delete' beside a bookmark removes it (C1)",
     HOME_CARD + "w" + READ_RIGHT + READ_MIDDLE + RMENU_MARKS + MARKS_THIS_PAGE
     + READ_MIDDLE + RMENU_MARKS + "T430,165;",
     lambda log, sd: "[Bookmarks] added" in log and "[Bookmarks] removed" in log),
    # C2: the frontlight from the book's menu (its last item), and back to that menu.
    ("book menu: Frontlight opens the light screen, back returns to the book menu (C2)",
     HOME_CARD + "w" + READ_MIDDLE + "T240,658;" + TITLE + RMENU_RESUME,
     lambda log, sd: "[Reader] Page" in log.split(">>> TAP_240_506")[-1]),
    ("power: sleep and wake",
     "P" + "Z",
     lambda log, sd: "Entering Deep Sleep" in log and log.count("drawHomeScreen") >= 2),
]

def run(script, config=None, srv=None):
    sd = tempfile.mkdtemp(); shutil.rmtree(sd)
    shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd)
    shutil.copy(os.path.join(ROOT, "test/sim/config.json"), os.path.join(sd, "config.json"))
    if srv is not None:   # the fake server's state (test/sim/fake_bookorbit.cpp)
        json.dump(srv, open(os.path.join(sd, "server.json"), "w"))
    if config:   # settings the case starts with (merged into config.json)
        p = os.path.join(sd, "config.json")
        c = json.load(open(p)); c.update(config); json.dump(c, open(p, "w"))
    frames = tempfile.mkdtemp()
    try:
        res = subprocess.run([BIN, frames, sd, script], capture_output=True, text=True, timeout=240)
    finally:
        shutil.rmtree(frames, ignore_errors=True)
    return res.returncode, res.stdout, sd

def main():
    failed = 0
    for case in CASES:
        name, script, check = case[:3]
        code, log, sd = run(script, case[3] if len(case) > 3 else None, case[4] if len(case) > 4 else None)
        problems = []
        if "KEY_NOT_ON_DEVICE" in log: problems.append("used a key Picco does not have")
        if "STUCK" in log: problems.append("stuck in a screen's input loop")
        if code not in (0,): problems.append(f"exit code {code}")
        try:
            ok = bool(check(log, sd))
        except Exception as e:
            ok = False; problems.append(f"check raised {e!r}")
        if not ok: problems.append("expected effect not seen")
        print(("ok    " if not problems else "FAIL  ") + name + ("" if not problems else "  -> " + "; ".join(problems)))
        if problems:
            failed += 1
            if os.environ.get("VERBOSE"): print(log[-3000:])
        shutil.rmtree(sd, ignore_errors=True)
    print(f"{'OK' if not failed else 'FAILED'}: {len(CASES) - failed}/{len(CASES)} Picco cases passed")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
