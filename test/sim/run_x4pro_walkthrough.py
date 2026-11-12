#!/usr/bin/env python3
"""Xteink X4 Pro walkthrough: every function reached with touch, GT911 Home Key
('k' short click / 'K' long press), and UP / DOWN / POWER keys only
(the x4pro build refuses physical L, R, C, and B keys).

    make -f test/sim/Makefile x4pro-test

Each case runs test/sim/build-x4pro/x4_headless on a fresh copy of
simulator/sdcard and checks both stdout logs and persisted SD state.
"""
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# Files a manual simulator session leaves on the card (uploaded offline books, their caches).
SD_LEFTOVERS = shutil.ignore_patterns("offline", "offline#*")
BIN = os.environ.get("X4PRO_HEADLESS") or os.path.join(ROOT, "test/sim/build-x4pro/x4_headless")

BACK = "W100,300,400,300;"        # swipe right = back
TITLE = "T240,20;"                # title bar back hit area
HOME_KEY_TAP = "k"                # GT911 Home Key short tap -> BTN_BACK click
HOME_KEY_LONG = "K"               # GT911 Home Key long press -> Reader menu (only in reading)
HOME_CARD = "T200,200;"
HOME_CATALOG = "T200,385;"
CATALOG_ALL_BOOKS = "T200,145;"
HOME_ALL_BOOKS = HOME_CATALOG + CATALOG_ALL_BOOKS
LIB_FRANKENSTEIN = "T200,225;"   # 3rd row (two-line rows, U1)
HOME_OFFLINE = "T200,445;"
HOME_SETTINGS = "T200,505;"
HOME_SYNC = "T200,565;"
READ_MIDDLE = "T240,400;"
READ_RIGHT = "T400,400;"
READ_LEFT = "T80,400;"
RMENU_TOC, RMENU_FONT, RMENU_MARKS = "T240,200;", "T240,278;", "T240,354;"
RMENU_SYNC, RMENU_RESUME, RMENU_CLOSE = "T240,430;", "T240,506;", "T240,582;"
MARKS_THIS_PAGE = "T240,90;"
APPEAR_TYPEFACE, APPEAR_SIZE = "T240,67;", "T240,103;"
APPEAR_ORIENTATION = "T100,427;"   # on the label: the middle of a value row is its "-" (C5)
SB_BOTTOM = "T240,393;"   # the "Bottom" card

# Settings rows (12 rows on X4 Pro: 0..9 shared + 10 USB-MSC + 11 Touch Wake on the second page)
SET_LANG = "T240,80;"
SET_WIFI = "T240,140;"
SET_SERVER = "T240,195;"
SET_SLEEP = "T240,250;"
SET_INFO = "T240,370;"
SET_SYNC = "T240,428;"
SET_LIGHT = "T240,486;"
SET_READING = "T240,544;"   # Settings -> Reading: status bar, full refresh, pictures
READING_STATUS_BAR = "T240,67;"
SET_ORIENTATION = "T240,602;"
SET_USB_MSC = "T240,660;"
SET_TOUCH_WAKE = "W240,600,240,200;T240,80;"   # the next page of the list, its first row
SET_ORIENTATION_LANDSCAPE = "T224,400;"

FL_BRIGHT_PLUS = "T442,124;"
FL_WARM_PLUS = "T442,240;"
FL_QUICK_ONOFF = "T240,320;"
USB_MSC_CONNECT = "T240,90;"
USB_MSC_TOGGLE = "T240,165;"
USB_MSC_EXIT_BTN = "T240,690;"


def pages(log):
    return [tuple(map(int, m)) for m in re.findall(r"\[Reader\] Page (\d+)/(\d+)", log)]


def config(sd):
    with open(os.path.join(sd, "config.json")) as f:
        return json.load(f)


CASES = [
    ("boot: x4pro reports touch, Home Key, USB-MSC, and INT wake pin",
     "",
     lambda log, sd: "[BOOT] device=XTEINK X4 PRO" in log
                     and "touch=1 homeKey=1 usbMsc=1 intWake=10" in log),
    ("home: swipe steps through recent books, tap opens the book",
     "W300,200,60,200;" + HOME_CARD + "w",
     lambda log, sd: "SWIPE_LEFT" in log and "Frankenstein (1818)" in log.split("SWIPE_LEFT")[-1] and pages(log)),
    ("reader: tap zones, swipes and UP/DOWN keys turn pages (up_down_pages default on)",
     HOME_CARD + "w" + READ_RIGHT + "W400,400,80,400;" + "D" + "U" + READ_LEFT + "W80,400,400,400;",
     lambda log, sd: [p for p, _ in pages(log)][-7:] == [1, 2, 3, 4, 3, 2, 1]),
    ("home key: long press in reading opens reader menu; short tap closes back to reading then home",
     HOME_CARD + "w" + HOME_KEY_LONG + HOME_KEY_TAP + HOME_KEY_TAP,
     lambda log, sd: "drawReaderMenuScreen" in log.split(">>> HOME_LONG", 1)[1]
                     and "[Reader] Page" in log.split(">>> HOME_TAP", 2)[1]
                     and "drawHomeScreen" in log.split(">>> HOME_TAP", 2)[2]),
    ("home key: short tap goes back from settings; long press is ignored outside reading",
     HOME_SETTINGS + HOME_KEY_LONG + HOME_KEY_TAP,
     lambda log, sd: "drawReaderMenuScreen" not in log
                     and "drawHomeScreen" in log.split(">>> HOME_TAP", 1)[1]),
    ("home key: long press is ignored inside modal; short tap closes modal",
     HOME_SETTINGS + SET_INFO + HOME_KEY_LONG + HOME_KEY_TAP + HOME_KEY_TAP,
     lambda log, sd: "drawReaderMenuScreen" not in log
                     and "drawSettingsScreen" in log.split(">>> HOME_TAP", 2)[1]
                     and "drawHomeScreen" in log.split(">>> HOME_TAP", 2)[2]),
    ("reader: while the page is read, the next chapters are laid out in the background",
     HOME_CARD + "w" + "w" + "w",
     lambda log, sd: "laid out in the background: ok" in log and "gave up" not in log),
    ("reader menu: appearance, font size list, tap 20 pt, back lays the book out",
     HOME_CARD + "w" + HOME_KEY_LONG + RMENU_FONT + APPEAR_SIZE + "T240,236;" + HOME_KEY_TAP,
     lambda log, sd: re.search(r"Chapter \d+( laid out|: first page)", log.split("TAP_240_236", 1)[1])
                     and config(sd)["font_pt"] == 20),
    ("reader menu: landscape pages, tap zones turn with them",
     HOME_CARD + "w" + HOME_KEY_LONG + RMENU_FONT + APPEAR_ORIENTATION + HOME_KEY_TAP + "w" + "T264,700;T264,700;T264,100;",
     lambda log, sd: config(sd)["orientation"] == 1 and [p for p, _ in pages(log)][-3:] == [2, 3, 2]),
    ("settings: Reading -> status bar menu, bottom line off gives text room",
     HOME_SETTINGS + SET_READING + READING_STATUS_BAR + SB_BOTTOM + SB_BOTTOM + TITLE + TITLE + TITLE + HOME_CARD + "w",
     lambda log, sd: config(sd)["sb_bottom"] == 2 and "[Reader] Page" in log.split("TAP_240_393")[-1]),
    ("reader menu: bookmarks, tap 'this page' adds bookmark",
     HOME_CARD + "w" + READ_RIGHT + HOME_KEY_LONG + RMENU_MARKS + MARKS_THIS_PAGE,
     lambda log, sd: "[Bookmarks] added" in log and os.path.isdir(os.path.join(sd, "bookmarks"))),
    ("library: delete modal OK removes book; Cancel keeps it",
     HOME_ALL_BOOKS + LIB_FRANKENSTEIN + "T240,256;" + "T340,475;",
     lambda log, sd: "Deleting downloaded book file: /books/Frankenstein.epub" in log
                     and not os.path.exists(os.path.join(sd, "books/Frankenstein.epub"))),
    ("settings: language by tap",
     HOME_SETTINGS + SET_LANG + "T240,165;w",
     lambda log, sd: config(sd)["language"] in ("ru", 1, "RU")),
    ("settings: frontlight sliders + quick On/Off button remember brightness and warmth",
     HOME_SETTINGS + SET_LIGHT + FL_QUICK_ONOFF + FL_BRIGHT_PLUS + FL_WARM_PLUS + FL_QUICK_ONOFF + FL_QUICK_ONOFF + HOME_KEY_TAP,
     lambda log, sd: "[Light] brightness 50%" in log
                     and "[Light] brightness 60%, warm 60%" in log
                     and "[Light] brightness 0%, warm 60%" in log
                     and log.split("[Light] brightness 0%, warm 60%", 1)[1].find("[Light] brightness 60%, warm 60%") >= 0
                     and config(sd)["frontlight"] == 60 and config(sd)["frontlight_warm"] == 60),
    ("settings: touch wake toggle persists across reboot",
     HOME_SETTINGS + SET_TOUCH_WAKE + HOME_KEY_TAP + "P" + "Z",
     lambda log, sd: config(sd)["touch_wake"] is False),
    ("settings: USB-MSC disable guard, enable, host state transitions, and exit button remount",
     HOME_SETTINGS + SET_USB_MSC + USB_MSC_TOGGLE + USB_MSC_CONNECT + HOME_KEY_TAP
     + USB_MSC_TOGGLE + USB_MSC_CONNECT + "McMaMeMdMi" + USB_MSC_EXIT_BTN + HOME_KEY_TAP + HOME_CARD + "w",
     lambda log, sd: config(sd)["usb_msc"] is True
                     and log.count("Detaching filesystem and starting USB Mass Storage") == 1
                     and "MSC_c]" in log
                     and "MSC_a]" in log
                     and "MSC_e]" in log
                     and "MSC_d]" in log
                     and "MSC_i]" in log
                     and "drawSettingsScreen" in log.split("TAP_240_690", 1)[1]
                     and pages(log)),
    ("settings: USB-MSC exits safely via Home Key tap",
     HOME_SETTINGS + SET_USB_MSC + USB_MSC_CONNECT + HOME_KEY_TAP,
     lambda log, sd: "Detaching filesystem" in log
                     and "MicroSD mounted successfully" in log.split(">>> HOME_TAP", 1)[1]
                     and "List /books:" in log.split(">>> HOME_TAP", 1)[1]
                     and "drawSettingsScreen" in log.split(">>> HOME_TAP", 1)[1]),
    ("settings: USB-MSC exits safely on POWER key without sleeping",
     HOME_SETTINGS + SET_USB_MSC + USB_MSC_CONNECT + "P",
     lambda log, sd: "Detaching filesystem" in log
                     and "Entering Deep Sleep" not in log
                     and "MicroSD mounted successfully" in log.split(">>> POWER", 1)[1]
                     and "drawSettingsScreen" in log.split(">>> POWER", 1)[1]),
    ("settings: BM8563 RTC & X4 Pro capabilities in Device Info",
     HOME_SETTINGS + SET_INFO + HOME_KEY_TAP,
     lambda log, sd: "[RTC] Ready (UTC 1735689600)" in log
                     and "device info" in log,
     None, {"SIM_RTC_UTC": "1735689600"}),
    ("settings: screen orientation turns interface by tap",
     HOME_SETTINGS + SET_ORIENTATION + SET_ORIENTATION_LANDSCAPE,
     lambda log, sd: config(sd)["ui_orientation"] == 2 and config(sd).get("orientation", 0) == 0),
    ("home in landscape: Settings row opens cleanly",
     "T250,620;",
     lambda log, sd: "drawSettingsScreen" in log, {"ui_orientation": 1}),
    ("offline: tap Offline library opens /offline and Home Key returns Home",
     HOME_OFFLINE + HOME_KEY_TAP,
     lambda log, sd: "List /offline: 0 rows" in log and "drawHomeScreen" in log.split(">>> HOME_TAP", 1)[1]),
    ("Wi-Fi: SSID typed on keyboard, Home Key tap cancels cleanly",
     HOME_SETTINGS + SET_WIFI + "[T60,385;T430,448;]" + SET_WIFI + HOME_KEY_TAP,
     lambda log, sd: config(sd)["wifi_ssid"].endswith("x")),
    ("power: sleep and wake",
     "P" + "Z",
     lambda log, sd: "Entering Deep Sleep" in log and log.count("drawHomeScreen") >= 2),
]


def run(script, cfg=None, env_extra=None):
    sd = tempfile.mkdtemp()
    shutil.rmtree(sd)
    shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd, ignore=SD_LEFTOVERS)
    shutil.copy(os.path.join(ROOT, "test/sim/config.json"), os.path.join(sd, "config.json"))
    if cfg:
        p = os.path.join(sd, "config.json")
        with open(p) as f:
            c = json.load(f)
        c.update(cfg)
        with open(p, "w") as f:
            json.dump(c, f)
    frames = tempfile.mkdtemp()
    env = os.environ.copy()
    if env_extra:
        env.update(env_extra)
    try:
        res = subprocess.run([BIN, frames, sd, script], capture_output=True, text=True, env=env, timeout=240)
    finally:
        shutil.rmtree(frames, ignore_errors=True)
    return res.returncode, res.stdout, sd


def main():
    failed = 0
    for case in CASES:
        name, script, check = case[:3]
        cfg = case[3] if len(case) > 3 else None
        env_extra = case[4] if len(case) > 4 else None
        code, log, sd = run(script, cfg, env_extra)
        problems = []
        if "KEY_NOT_ON_DEVICE" in log:
            problems.append("used a key X4 Pro does not have")
        if "STUCK" in log:
            problems.append("stuck in a screen's input loop")
        if code not in (0,):
            problems.append(f"exit code {code}")
        try:
            ok = bool(check(log, sd))
        except Exception as e:
            ok = False
            problems.append(f"check raised {e!r}")
        if not ok:
            problems.append("expected effect not seen")
        print(("ok    " if not problems else "FAIL  ") + name + ("" if not problems else "  -> " + "; ".join(problems)))
        if problems:
            failed += 1
            if os.environ.get("VERBOSE"):
                print(log[-3000:])
        shutil.rmtree(sd, ignore_errors=True)
    print(f"{'OK' if not failed else 'FAILED'}: {len(CASES) - failed}/{len(CASES)} X4 Pro cases passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
