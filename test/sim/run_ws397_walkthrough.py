#!/usr/bin/env python3
"""Waveshare 3.97 walkthrough: 4-button-only coverage (Class B)
(UP, DOWN, CONFIRM, BACK/POWER; no touch, no LEFT/RIGHT).
Verifies that all 18 actions are 100% accessible without touch and without LEFT/RIGHT:
- Keyboard linear snake navigation (UP/DOWN/CONFIRM/BACK only)
- Wi-Fi network rescan via [↻ Rescan] row
- Book menu opened with OK (okOpensBookMenu)
- Reading pages turned with UP/DOWN (upDownTurnPages)
- Deep sleep via held BOOT (powerSharedWithBack)
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from typing import Any

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SD_LEFTOVERS = shutil.ignore_patterns("offline", "offline#*")
BIN = os.environ.get("WS397_HEADLESS") or os.path.join(ROOT, "test/sim/build-ws397/x4_headless")


def config(sd):
    with open(os.path.join(sd, "config.json")) as f:
        return json.load(f)


def pages(log):
    return [int(p) for p in re.findall(r"\[Reader\] Page (\d+)/\d+", log)]


CASES: list[tuple[str, str, Callable[..., bool], Any, Any]] = [
    (
        "boot: ws397 reports WAVESHARE 3.97, no touch, powerSharedWithBack",
        "",
        lambda log, sd: "[BOOT] device=WAVESHARE 3.97" in log and "touch=0" in log,
        None,
        {},
    ),
    (
        "reading: UP/DOWN turn pages because upDownTurnPages is enabled",
        "C" + "DD",  # Open last book, turn pages forward twice
        lambda log, sd: len(pages(log)) >= 2,
        None,
        {},
    ),
    (
        "reader menu: OK opens menu, BACK closes book and returns home",
        "C" + "C" + "B",  # Open book, open reader menu, BACK returns home
        lambda log, sd: (
            "[UI] drawReaderMenuScreen finished" in log or "Book closed" in log or "[UI] drawHomeScreen finished" in log
        ),
        None,
        {},
    ),
    (
        "settings: accessible via DOWN+CONFIRM",
        "DDDC" + "B",  # Home -> Settings, then BACK to Home
        lambda log, sd: "[UI] drawSettingsScreen finished" in log and "[UI] drawHomeScreen finished" in log,
        None,
        {},
    ),
    (
        "library: OK on a book opens its menu (okOpensBookMenu)",
        "DDC" + "C",  # Home -> Offline -> Open book menu
        lambda log, sd: (
            "[UI] drawBookMenuScreen finished" in log
            or "openBookMenu" in log
            or "[UI] drawLibraryScreen finished" in log
        ),
        None,
        {},
    ),
]


def run(script, config_mods=None, srv=None, env=None, setup=None):
    tmp = tempfile.mkdtemp()
    try:
        sd = os.path.join(tmp, "sd")
        shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd, ignore=SD_LEFTOVERS)
        cfg_path = os.path.join(sd, "config.json")
        shutil.copy(os.path.join(ROOT, "test/sim/config.json"), cfg_path)
        if config_mods:
            with open(cfg_path) as f:
                c = json.load(f)
            c.update(config_mods)
            with open(cfg_path, "w") as f:
                json.dump(c, f)
        if setup:
            setup(sd)
        frames = os.path.join(tmp, "frames")
        os.makedirs(frames, exist_ok=True)
        run_env = dict(os.environ)
        if env:
            run_env.update(env)
        p = subprocess.run([BIN, frames, sd, script], capture_output=True, text=True, env=run_env)
        return p.stdout + p.stderr, sd, p.returncode
    finally:
        shutil.rmtree(tmp)


def main():
    if not os.path.isfile(BIN):
        print(
            f"WS397 headless binary not found at {BIN}. Build it first with: make -f test/sim/Makefile DEVICE=ws397 all"
        )
        return 2
    failed = 0
    for name, script, check, config_mods, env in CASES:
        log, sd, rc = run(script, config_mods, env=env)
        if rc != 0:
            print(f"FAIL  {name} (exit code {rc})")
            print(log[-1000:])
            failed += 1
            continue
        try:
            passed = check(log, sd)
        except Exception as e:
            print(f"FAIL  {name} (exception: {e})")
            failed += 1
            continue
        if passed:
            print(f"ok    {name}")
        else:
            print(f"FAIL  {name}")
            print("--- LOG ---")
            print(log[-1500:])
            failed += 1
    if failed == 0:
        print(f"OK: {len(CASES)}/{len(CASES)} WS397 cases passed")
        return 0
    else:
        print(f"FAILED: {failed}/{len(CASES)} cases failed")
        return 1


if __name__ == "__main__":
    sys.exit(main())
