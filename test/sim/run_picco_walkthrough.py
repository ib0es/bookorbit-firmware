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
RMENU_TOC, RMENU_FONT, RMENU_SYNC, RMENU_RESUME, RMENU_CLOSE = "T240,200;", "T240,280;", "T240,355;", "T240,430;", "T240,505;"
APPEAR_TYPEFACE, APPEAR_SIZE = "T240,67;", "T240,103;"   # rows of the Appearance screen
SET_LANG, SET_WIFI, SET_SERVER, SET_SLEEP, SET_INFO, SET_LIGHT = "T240,90;", "T240,165;", "T240,240;", "T240,315;", "T240,520;", "T240,665;"
SET_SYNC = "T240,593;"

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
    ("reader menu: table of contents, tap a chapter",
     HOME_CARD + "w" + READ_MIDDLE + RMENU_TOC + "T240,300;",
     lambda log, sd: len(pages(log)) >= 2 and pages(log)[-1][0] != pages(log)[-2][0]),
    ("reader menu: back by title tap, close book returns home",
     HOME_CARD + "w" + READ_MIDDLE + TITLE + READ_MIDDLE + RMENU_CLOSE,
     lambda log, sd: "drawHomeScreen" in log.split("TAP_240_505")[-1]),
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
     lambda log, sd: "Alice in Wonderland.fb2 downloaded successfully" in log.split("TAP_200_267", 1)[1]
     and "Opening book: /books/Alice in Wonderland.fb2" in log),
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
    ("power: sleep and wake",
     "P" + "Z",
     lambda log, sd: "Entering Deep Sleep" in log and log.count("drawHomeScreen") >= 2),
]

def run(script):
    sd = tempfile.mkdtemp(); shutil.rmtree(sd)
    shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd)
    shutil.copy(os.path.join(ROOT, "test/sim/config.json"), os.path.join(sd, "config.json"))
    frames = tempfile.mkdtemp()
    try:
        res = subprocess.run([BIN, frames, sd, script], capture_output=True, text=True, timeout=240)
    finally:
        shutil.rmtree(frames, ignore_errors=True)
    return res.returncode, res.stdout, sd

def main():
    failed = 0
    for name, script, check in CASES:
        code, log, sd = run(script)
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
