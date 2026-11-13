#!/usr/bin/env python3
"""Screenshots from the headless simulator.

Runs test/sim/build-x4/x4_headless on a fresh copy of simulator/sdcard with a button
script and writes every screen update as PNG, plus a contact sheet.

    make -f test/sim/Makefile test/sim/build-x4/x4_headless
    python3 tools/sim_shots.py OUT_DIR "SCRIPT" [--server JSON] [--sheet-cols 4]

Script letters (see test/sim/headless_main.cpp): U D L R C B P = buttons,
w = wait 2.2 s, S = apply server_next.json, Z = sleep and wake up.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get("X4_HEADLESS") or os.path.join(ROOT, "test/sim/build-x4/x4_headless")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("script")
    ap.add_argument("--server", default="{}", help="fake server state (server.json)")
    ap.add_argument("--config", default="{}", help='JSON merged into config.json, e.g. \'{"language": "es"}\'')
    ap.add_argument("--sheet-cols", type=int, default=4)
    ap.add_argument("--keep-sd", action="store_true")
    args = ap.parse_args()

    sd = tempfile.mkdtemp()
    shutil.rmtree(sd)
    shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd)
    with open(os.path.join(ROOT, "test/sim/config.json")) as f:
        cfg = json.load(f)
    cfg.update(json.loads(args.config))
    with open(os.path.join(sd, "config.json"), "w") as f:
        json.dump(cfg, f, ensure_ascii=False)
    with open(os.path.join(sd, "server.json"), "w") as f:
        json.dump(json.loads(args.server), f)
    frames = tempfile.mkdtemp()
    res = subprocess.run([BIN, frames, sd, args.script], capture_output=True, text=True, timeout=300)
    if res.returncode != 0:
        sys.stderr.write(res.stdout[-4000:] + res.stderr[-4000:])
    os.makedirs(args.out, exist_ok=True)
    for old in glob.glob(os.path.join(args.out, "*.png")):
        os.remove(old)
    pngs = []
    for pgm in sorted(glob.glob(os.path.join(frames, "*.pgm"))):
        png = os.path.join(args.out, os.path.basename(pgm)[:-4] + ".png")
        Image.open(pgm).convert("L").save(png)
        pngs.append(png)
    with open(os.path.join(args.out, "log.txt"), "w") as f:
        f.write(res.stdout)
    if pngs:
        ims = [Image.open(p) for p in pngs]
        w, h = ims[0].size
        cols = min(args.sheet_cols, len(ims))
        rows = (len(ims) + cols - 1) // cols
        scale = 0.5
        sw, sh = int(w * scale), int(h * scale)
        sheet = Image.new("L", (cols * (sw + 8) + 8, rows * (sh + 8) + 8), 160)
        for i, im in enumerate(ims):
            sheet.paste(im.resize((sw, sh)), (8 + (i % cols) * (sw + 8), 8 + (i // cols) * (sh + 8)))
        sheet.save(os.path.join(args.out, "sheet.png"))
    print(f"{len(pngs)} frames -> {args.out}")
    shutil.rmtree(frames, ignore_errors=True)
    if not args.keep_sd:
        shutil.rmtree(sd, ignore_errors=True)
    else:
        print("sd:", sd)


if __name__ == "__main__":
    main()
