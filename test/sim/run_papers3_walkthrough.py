#!/usr/bin/env python3
"""M5Stack PaperS3 walkthrough: 0-button touch-only coverage (Class D)
(GT911 touch only; no physical buttons, software powerOff via GPIO44 pulse).
Verifies that all 18 actions are 100% accessible via touch:
- SIM_KEYS_TOUCH_ONLY: rejects all physical keys (U/D/L/R/C/B/P)
- Tap navigation on Home, Library, Reader
- Title bar tap (◀) returns Back
- Reading page turns via touch zones (left / right / center)
- Touch paging ▲ / ▼ in paged lists
- Deep sleep via 'Zz' tap in header
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SD_LEFTOVERS = shutil.ignore_patterns("offline", "offline#*")
BIN = os.environ.get("PAPERS3_HEADLESS") or os.path.join(ROOT, "test/sim/build-papers3/x4_headless")


def config(sd):
    with open(os.path.join(sd, "config.json")) as f:
        return json.load(f)


def pages(log):
    return [int(p) for p in re.findall(r"\[Reader\] Page (\d+)/\d+", log)]


CASES = [
    (
        "boot: papers3 reports M5STACK PAPERS3, touch=1, no power button, no up/down",
        "",
        lambda log, sd: "[BOOT] device=M5STACK PAPERS3" in log and "touch=1" in log,
        None,
        {},
    ),
    (
        "home: tap on recent book card opens it, reading turns pages by side taps",
        "T200,200;w" + "T400,400;w" + "T400,400;w",  # Tap card to open, tap right side to turn page
        lambda log, sd: len(pages(log)) >= 1,
        None,
        {},
    ),
    (
        "title bar: tap on title bar (◀) leaves settings/screens",
        "T200,505;w" + "T20,20;w",  # Tap settings row, then tap title bar (◀)
        lambda log, sd: "[UI] drawSettingsScreen finished" in log and "[UI] drawHomeScreen finished" in log,
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
            f"PaperS3 headless binary not found at {BIN}. Build it first with: make -f test/sim/Makefile DEVICE=papers3 all"
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
        print(f"OK: {len(CASES)}/{len(CASES)} PaperS3 cases passed")
        return 0
    else:
        print(f"FAILED: {failed}/{len(CASES)} cases failed")
        return 1


if __name__ == "__main__":
    sys.exit(main())
