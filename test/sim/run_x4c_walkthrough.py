#!/usr/bin/env python3
"""Xteink X4 Classic walkthrough: physical-button-only coverage for X4C specifics
(8 physical buttons U/D/L/R/C/B/P, no touch, no frontlight, QMI8658 auto-rotate
enabled by default, BM8563 RTC, button-only USB-MSC workflow, and idle deep sleep
guard while USB Serial is open at 100% battery).

    make -f test/sim/Makefile x4c-test
"""
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.environ.get("X4C_HEADLESS") or os.path.join(ROOT, "test/sim/build-x4c/x4_headless")

OPEN_BOOK = "C"
READER_MENU = "C"
APPEARANCE = READER_MENU + "DC"
SETTINGS = "DDC"  # Home -> Settings (row 0 = Language; 9 items total: 0..8)


def config(sd):
    with open(os.path.join(sd, "config.json")) as f:
        return json.load(f)


def pages(log):
    return [int(p) for p in re.findall(r"\[Reader\] Page (\d+)/\d+", log)]


def after(log, marker):
    i = log.rfind(marker)
    return log[i:] if i >= 0 else ""


CASES = [
    ("boot: x4c reports XTEINK X4 CLASSIC, no touch/homeKey, USB-MSC, RTC, and IMU",
     "",
     lambda log, sd: "[BOOT] device=XTEINK X4 CLASSIC" in log
                     and "touch=0 homeKey=0 usbMsc=1 intWake=-1 rtc=1 imu=1" in log
                     and "[RTC] Ready (UTC 1735689600)" in log
                     and "[IMU] Ready" in log,
     None, {"SIM_RTC_UTC": "1735689600", "SIM_IMU_PRESENT": "1"}),

    ("settings: 9-row button layout (Orientation=7, USB Drive=8, Device Info=5, no frontlight)",
     SETTINGS + "DDDDDC" + "B",
     lambda log, sd: "device info" in log
                     and "[RTC] Ready (UTC 1735689600)" in log
                     and "[UI] drawSettingsScreen finished" in log,
     None, {"SIM_RTC_UTC": "1735689600", "SIM_IMU_PRESENT": "1"}),

    ("imu: auto_rotate is ON by default on X4 Classic, rotates on gravity, sleeps when disabled",
     "A-1.0,0.0,0.0;w" + SETTINGS + "UUC",
     lambda log, sd: "[IMU] Ready" in log
                     and "[App] Auto-rotated UI 0 -> 1" in log
                     and "[IMU] Put to sleep" in log
                     and config(sd).get("auto_rotate") is False,
     None, {"SIM_IMU_PRESENT": "1"}),

    ("usb-msc: pure button workflow (disable guard, enable, host events, CONFIRM & BACK exits)",
     SETTINGS + "UC"       # Settings -> wrap up to USB Drive (index 8) -> submenu (0 = Connect)
     + "DC"                # Toggle USB Drive Mode OFF
     + "UC"                # Attempt Connect while disabled -> modal guard
     + "B"                 # Close modal
     + "DC"                # Toggle USB Drive Mode back ON
     + "UC"                # Connect -> STATE_USB_MSC (button-only screen)
     + "McMaMeMdMi"        # Simulated USB host state transitions
     + "C"                 # Exit via physical CONFIRM -> remounts SD -> Settings
     + "C" + "C"           # Re-enter USB Drive (cursor remembered at 0 = Connect) -> Connect
     + "B"                 # Exit via physical BACK -> remounts SD -> Settings
     + "B" + "UU" + OPEN_BOOK + "w",
     lambda log, sd: config(sd).get("usb_msc") is True
                     and config(sd).get("auto_rotate") is True
                     and "Detaching filesystem and starting USB Mass Storage" in log
                     and log.count("Detaching filesystem and starting USB Mass Storage") == 2
                     and log.count("MicroSD mounted successfully") == 3
                     and "MSC_c]" in log and "MSC_a]" in log and "MSC_e]" in log and "MSC_d]" in log and "MSC_i]" in log
                     and pages(log)),

    # B8: in USB mode with no computer (never connected here) for the idle timeout
    # (3 s in this case), the mode is left; the usual idle sleep follows.
    # U3: auto-rotate (on by default here) turned off from the book's Appearance,
    # its last row on a board with a motion sensor.
    ("appearance: auto-rotate toggled without leaving the book (U3)",
     OPEN_BOOK + "C" + "DC" + "U" + "R",
     lambda log, sd: config(sd).get("auto_rotate") is False,
     None, {"SIM_IMU_PRESENT": "1"}),
    ("usb-msc: no computer for the idle timeout leaves the mode, then the device sleeps (B8)",
     SETTINGS + "UCC" + "www",
     lambda log, sd: "Detaching filesystem" in log
                     and "No computer for the idle timeout: leaving USB mass storage" in log
                     and log.count("MicroSD mounted successfully") == 2
                     and "Entering Deep Sleep" in after(log, "leaving USB mass storage"),
     None, {"SIM_SLEEP_TIMEOUT_MS": "3000", "SIM_USB": "0"}),
    ("usb-msc: POWER exits USB-MSC safely without sleeping, then POWER sleeps/wakes normally",
     SETTINGS + "UCC" + "P" + "B" + "PZ",
     lambda log, sd: "Detaching filesystem" in log
                     and log.count("Entering Deep Sleep") == 1
                     and "MicroSD mounted successfully" in log.split(">>> POWER", 1)[1].split(">>> BACK")[0]
                     and log.count("drawHomeScreen") >= 2),

    ("power: open USB Serial blocks idle deep sleep even at 100% battery (STAT LOW)",
     OPEN_BOOK + "w",
     lambda log, sd: "Idle timeout reached" not in log and "Entering Deep Sleep" not in log,
     None, {"SIM_BATTERY_PCT": "100", "SIM_BATTERY_VOLTS": "4.20", "SIM_SLEEP_TIMEOUT_MS": "400", "SIM_USB_SERIAL": "1"}),

    ("power: without USB Serial, 100% battery enters idle deep sleep after timeout",
     OPEN_BOOK + "w",
     lambda log, sd: "Idle timeout reached" in log and "Entering Deep Sleep" in log,
     None, {"SIM_BATTERY_PCT": "100", "SIM_BATTERY_VOLTS": "4.20", "SIM_SLEEP_TIMEOUT_MS": "400", "SIM_USB_SERIAL": "0"}),

    ("rtc: BM8563 clock and UTC+3 timezone configured with buttons and shown in status bar",
     OPEN_BOOK + APPEARANCE + "U" + "C" + "UU" + "C" + "D" + "RRRRRR" + "BB",
     lambda log, sd: "[RTC] Ready" in log and "[Clock] Restored from RTC" in log
                     and config(sd).get("sb_clock") is True and config(sd).get("utc_offset_min") == 180
                     and "Page drawn" in after(log, ">>> BACK"),
     None, {"SIM_RTC_UTC": "1794391200"}),

    ("orientation: 180-deg turn swaps LEFT/RIGHT keys (RIGHT acts as Back from Settings)",
     SETTINGS + "UU" + "CCC" + "R",
     lambda log, sd: config(sd).get("auto_rotate") is False
                     and config(sd).get("ui_orientation") == 2
                     and "drawHomeScreen" in after(log, ">>> RIGHT"),
     None, {"SIM_IMU_PRESENT": "1"}),
]


def run(script, cfg=None, env_extra=None):
    sd = tempfile.mkdtemp()
    shutil.rmtree(sd)
    shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd)
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
        if "STUCK" in log:
            problems.append("stuck in a screen's input loop")
        if "[Buttons] Event: TAP" in log or "SWIPE_" in log:
            problems.append("unexpected touch input on button-only walkthrough")
        if code != 0:
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
    print(f"{'OK' if not failed else 'FAILED'}: {len(CASES) - failed}/{len(CASES)} X4 Classic cases passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
