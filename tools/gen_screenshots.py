#!/usr/bin/env python3
"""Generates the README screenshots (English, Xteink X4, portrait) with the headless simulator.

    make -f test/sim/Makefile all
    python3 tools/gen_screenshots.py            (writes assets/screenshots/*.png)

Needs Pillow. Each scene is a key script (U/D/L/R/C/B: the front keys, w: wait) run on a copy of
the simulator card with the test configuration; the last frame it drew is the screenshot.
"""

import http.server
import json
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "test/sim/build-x4/x4_headless"
OUT = ROOT / "assets/screenshots"

SCENES = {
    "home": "",
    "catalog": "DC",
    "library": "DCDCC",
    "reading": "Cww" + "R" * 6 + "w",  # a page of plain text, not the cover or the title page
    "reader-menu": "CwwC",
    "settings": "UC",
    "language": "UCDDDDDDC",
    "keyboards": "UCDDDDDDDC",
    "search-keyboard": "DCUC",
}
PORTAL_SCENES = {"web-qr": "UCDDDDDCC"}  # Settings -> Web -> the access point portal


def render(script: str, portal: bool = False) -> Image.Image:
    with tempfile.TemporaryDirectory() as tmp:
        card, frames = Path(tmp, "sd"), Path(tmp, "frames")
        shutil.copytree(ROOT / "simulator/sdcard", card, ignore=shutil.ignore_patterns("offline", "offline#*"))
        frames.mkdir()
        config = json.loads((ROOT / "test/sim/config.json").read_text())
        config["language"] = "en"
        (card / "config.json").write_text(json.dumps(config))
        if portal:  # the portal closes itself on the first request: the frame before that one is its screen
            close = {"method": "POST", "uri": "/api/close", "form": {}, "files": [], "headers": {}}
            (card / "portal_requests.json").write_text(json.dumps([close]))
        subprocess.run(
            [BIN, frames, card, script], capture_output=True, text=True, timeout=120
        )  # a nested input loop exits non-zero
        shots = sorted(frames.glob("*.pgm"))
        last = shots[-2] if portal else shots[-1]
        return Image.open(last).convert("L")


def trim(image: Image.Image) -> Image.Image:
    """Cuts the empty page background under the content."""
    background = image.getpixel((2, image.height - 2))
    rows = [
        y for y in range(image.height) if any(image.getpixel((x, y)) != background for x in range(0, image.width, 4))
    ]
    return image.crop((0, 0, image.width, min(image.height, rows[-1] + 20)))


CHROME = Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
PORTAL_BOOKS = ["Dune - Frank Herbert.epub", "Neuromancer - William Gibson.epub", "The Left Hand of Darkness.fb2"]


TABS = {"fonts": 1, "settings": 2}  # the buttons of the page, left to right


def render_web_portal(tab: str = "books") -> Image.Image:
    """The web configuration page, served from the portal handlers of the simulator build."""
    sys.path.insert(0, str(ROOT / "test/sim"))
    import run_portal_server as portal

    sizes = ",".join("3000" for _ in PORTAL_BOOKS)
    run = portal.run(
        [
            portal.post(f"/api/upload?sizes={sizes}", files=[portal.book(name) for name in PORTAL_BOOKS]),
            portal.get("/"),
            portal.get("/api/books"),
            portal.get("/api/fonts"),
        ],
        setup=portal.with_fonts("Literata", "NotoSans"),  # families on the card, as after an upload
    )
    page = run.resp[1]["body"]
    if tab != "books":  # open another tab the way a tap on it would
        page = page.replace(
            "</body>", f'<script>showTab("{tab}",document.querySelectorAll(".tab")[{TABS[tab]}])</script></body>'
        )
    bodies = {"/": page, "/api/books": run.resp[2]["body"], "/api/fonts": run.resp[3]["body"]}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = bodies.get(self.path.split("?")[0])
            self.send_response(200 if body is not None else 404)
            self.send_header(
                "Content-Type", "application/json" if self.path.startswith("/api") else "text/html; charset=utf-8"
            )
            self.end_headers()
            self.wfile.write((body or "").encode())

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory() as tmp:
            png = Path(tmp, "portal.png")
            subprocess.run(
                [
                    CHROME,
                    "--headless=new",
                    "--disable-gpu",
                    "--hide-scrollbars",
                    "--window-size=620,2600",
                    "--virtual-time-budget=4000",
                    f"--screenshot={png}",
                    f"http://127.0.0.1:{server.server_port}/",
                ],
                capture_output=True,
                timeout=120,
                check=True,
            )
            return trim(Image.open(png).convert("RGB"))
    finally:
        server.shutdown()


def main() -> int:
    if not BIN.exists():
        print(f"Build the simulator first: make -f test/sim/Makefile all ({BIN} is missing)", file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    for name, script in SCENES.items():
        render(script).save(OUT / f"{name}.png", optimize=True)
        print(f"wrote {OUT.relative_to(ROOT)}/{name}.png")
    for name, script in PORTAL_SCENES.items():
        render(script, portal=True).save(OUT / f"{name}.png", optimize=True)
        print(f"wrote {OUT.relative_to(ROOT)}/{name}.png")
    if CHROME.exists():
        for name, tab in (("web-books", "books"), ("web-fonts", "fonts"), ("web-config", "settings")):
            render_web_portal(tab).save(OUT / f"{name}.png", optimize=True)
            print(f"wrote {OUT.relative_to(ROOT)}/{name}.png")
    else:
        print("Chrome not found: web screenshots not regenerated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
