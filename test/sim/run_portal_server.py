#!/usr/bin/env python3
"""The web portal's SERVER, every handler of src/net/web_config.cpp, in the simulator.

    make -f test/sim/Makefile all
    python3 test/sim/run_portal_server.py            (VERBOSE=1: the answers of a failed case)
    python3 test/sim/run_portal_server.py "<part of a name>"

The real portal runs (Settings -> Web -> portal) with a stand-in for the ESP32 WebServer
(test/sim/stubs_net/WebServer.h) that plays the requests a case writes to
<card>/portal_requests.json, the way a browser would send them, uploads included. Every
answer is read back, and so are the files the requests must have left on the card. The page
the browser runs is tested apart: test/host/test_portal_js.js.

A home Wi-Fi portal asks for a PIN (777777 here: the stub's random number is always 7);
the access point portal asks for none.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SD_LEFTOVERS = shutil.ignore_patterns("offline#*")
BIN = os.environ.get("X4_HEADLESS") or os.path.join(ROOT, "test/sim/build-x4/x4_headless")
PIN = "777777"
WEB_MENU = "UC" + "DDDDDDC"  # home -> Settings (last tile) -> Web (seventh row)
AP = WEB_MENU + "C"  # the access point portal
STA = WEB_MENU + "D" + "C"  # the home Wi-Fi portal (asks for the PIN)
TTF = "00010000"  # the first bytes of a TrueType font


# ---------------------------------------------------------------- requests
def get(uri, headers=None):
    return {"method": "GET", "uri": uri, "headers": headers or {}}


def post(uri, form=None, files=None, headers=None):
    return {"method": "POST", "uri": uri, "form": form or {}, "files": files or [], "headers": headers or {}}


def book(name, size=3000):
    return {"name": name, "size": size, "fill": "b"}


def font(name, size=2000, hex_=TTF):
    return {"name": name, "size": size, "hex": hex_, "fill": "f"}


CLOSE = post("/api/close")


def pin(uri):
    return uri + ("&" if "?" in uri else "?") + "pin=" + PIN


# ---------------------------------------------------------------- the card
def card(sd, *parts):
    return os.path.join(sd, *[p.lstrip("/") for p in parts])


def files(sd, pattern):
    import glob

    return sorted(glob.glob(card(sd, pattern)))


def read(sd, path):
    return open(card(sd, path), "rb").read()


def put(sd, path, data):
    os.makedirs(os.path.dirname(card(sd, path)), exist_ok=True)
    open(card(sd, path), "wb").write(data if isinstance(data, bytes) else data.encode())


def cfg(sd):
    return json.load(open(card(sd, "config.json")))


def merge_config(sd, **kv):
    c = cfg(sd)
    c.update(kv)
    json.dump(c, open(card(sd, "config.json"), "w"))


def with_fonts(*names):
    """A card with the font families `names` (the repository's own fonts) in /fonts."""

    def setup(sd):
        shutil.rmtree(card(sd, "fonts"), ignore_errors=True)
        os.makedirs(card(sd, "fonts"))
        partition = card(sd, ".fontpart.bin")
        if os.path.exists(partition):
            os.remove(partition)
        for name in names:
            shutil.copy(
                os.path.join(ROOT, "fonts", "sd", name, name + "-Regular.ttf"), card(sd, "fonts", name + "-Regular.ttf")
            )

    return setup


def font_files(sd, family):
    """Every file of /fonts (in its family folders too) whose name has `family`."""
    found = []
    for base, _, names in os.walk(card(sd, "fonts")):
        found += [os.path.join(base, n) for n in names if family.lower() in n.lower()]
    return sorted(found)


def flash_families(sd):
    """The families the Flash catalog holds (read back from the font partition)."""
    data = open(card(sd, ".fontpart.bin"), "rb").read()
    newest = None
    for d in (data[0x360000 - 8192 :], data[0x360000 - 4096 :]):
        if d[:4] == b"FIB2" and (
            newest is None or int.from_bytes(d[8:12], "little") > int.from_bytes(newest[8:12], "little")
        ):
            newest = d
    return int.from_bytes(newest[12:16], "little") if newest else 0


# ---------------------------------------------------------------- one run
class Run:
    def __init__(self, responses, log, sd):
        self.resp = responses
        self.log = log
        self.sd = sd
        self.problems = []

    def expect(self, cond, what):
        if not cond:
            self.problems.append(what)

    def json(self, i):
        try:
            return json.loads(self.resp[i]["body"])
        except Exception:
            return None

    def code(self, i):
        return self.resp[i]["code"] if i < len(self.resp) else None


def run(requests, setup=None, script=AP, config=None, own_close=False):
    sd = tempfile.mkdtemp()
    shutil.rmtree(sd)
    shutil.copytree(os.path.join(ROOT, "simulator/sdcard"), sd, ignore=SD_LEFTOVERS)
    shutil.copy(os.path.join(ROOT, "test/sim/config.json"), card(sd, "config.json"))
    for f in ("server.json", "server_next.json"):
        if os.path.exists(card(sd, f)):
            os.remove(card(sd, f))
    shutil.rmtree(card(sd, "offline"), ignore_errors=True)
    os.makedirs(card(sd, "offline"))
    if config:
        merge_config(sd, **config)
    if setup:
        setup(sd)
    reqs = list(requests)
    if not own_close:  # a case that closes the portal itself says so
        reqs.append(post(pin("/api/close")) if script == STA else CLOSE)  # the portal's loop must end
    json.dump(reqs, open(card(sd, "portal_requests.json"), "w"))
    frames = tempfile.mkdtemp()
    try:
        res = subprocess.run([BIN, frames, sd, script], capture_output=True, text=True, timeout=120)
    finally:
        shutil.rmtree(frames, ignore_errors=True)
    responses = []
    for line in res.stdout.splitlines():
        if line.startswith("[PortalResp] "):
            responses.append(json.loads(line[len("[PortalResp] ") :]))
    r = Run(responses, res.stdout, sd)
    if res.returncode != 0:
        r.problems.append(f"exit code {res.returncode}")
    if "STUCK" in res.stdout:
        r.problems.append("stuck: the portal never closed")
    r.expect(len(responses) == len(reqs), f"{len(responses)} answers for {len(reqs)} requests")
    return r


# ---------------------------------------------------------------- the cases
CASES: list[tuple[str, Callable[[], "Run"]]] = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn

    return deco


# ---- the page and the lists
@case("page: the portal page is served, filled in, and not cached")
def _():
    r = run([get("/")])
    page = r.resp[0]["body"] if r.resp else ""
    r.expect(r.code(0) == 200 and r.resp[0]["type"].startswith("text/html"), "the page answers 200 text/html")
    r.expect("<form" in page and "id='btn-up'" in page, "the page has the settings form and the Upload button")
    r.expect(
        not re.search(r"%[A-Z][A-Z_]{3,}%", page),
        f"placeholders are left in the page: {re.findall(r'%[A-Z][A-Z_]{3,}%', page)[:5]}",
    )
    r.expect("no-store" in r.resp[0]["headers"].get("Cache-Control", ""), "the page is not cached")
    return r


@case("page: the saved settings are in the form, a password is not")
def _():
    r = run([get("/")], config={"username": "reader42", "server_url": "http://example.test:3000"})
    page = r.resp[0]["body"] if r.resp else ""
    r.expect("reader42" in page and "example.test" in page, "the user and the server are in the form")
    r.expect("test-password" not in page, "the Wi-Fi password is not sent back to the browser")
    return r


@case("lists: the offline books are listed with their sizes")
def _():
    def setup(sd):
        put(sd, "offline/One.epub", b"x" * 1500)
        put(sd, "offline/Two.fb2", b"y" * 700)
        put(sd, "offline/notes.pdf", b"z" * 10)

    r = run([get("/api/books")], setup)
    d = r.json(0) or {}
    names = sorted((b["name"], b["size"]) for b in d.get("books", []))
    r.expect(
        names == [("One.epub", 1500), ("Two.fb2", 700)],
        f"the books are One.epub and Two.fb2, with their sizes: {names}",
    )
    r.expect(d.get("total", 0) > 0 and "free" in d, "the card's space is told")
    return r


@case("lists: the fonts of the card and the Flash state are listed")
def _():
    r = run([get("/api/fonts")], with_fonts("Literata", "NotoSans"))
    d = r.json(0) or {}
    r.expect(
        sorted(f["name"] for f in d.get("families", [])) == ["Literata", "NotoSans"],
        f"both families are listed: {d.get('families')}",
    )
    r.expect(
        all("fits" in f and f["installed"] is False for f in d.get("families", [])),
        "each says if it fits and is not installed yet",
    )
    r.expect(
        d.get("builtin") and d.get("capacity", 0) > 0 and d.get("installedFamilies") == [],
        "the built-in font and the Flash room are told",
    )
    return r


@case("access point: the captive-portal probes are sent to the portal")
def _():
    r = run([get("/generate_204"), get("/hotspot-detect.html"), get("/anything/else")])
    for i in range(3):
        r.expect(
            r.code(i) == 302 and "192.168.4.1" in r.resp[i]["headers"].get("Location", ""),
            f"request {i} is redirected to the portal",
        )
    return r


@case("home Wi-Fi: a probe or an unknown address is not redirected")
def _():
    r = run([get(pin("/generate_204")), get(pin("/nothing"))], script=STA)
    r.expect(r.code(0) == 404 and r.code(1) == 404, "answered 404, no redirect")
    return r


# ---- books
@case("books: an upload is stored in /offline, no .part is left")
def _():
    r = run([post("/api/upload?sizes=3000,1200", files=[book("Alpha.epub", 3000), book("Beta.fb2", 1200)])])
    d = r.json(0) or {}
    r.expect(r.code(0) == 200 and d.get("ok") is True and len(d.get("files", [])) == 2, f"both files are reported: {d}")
    r.expect(
        os.path.getsize(card(r.sd, "offline/Alpha.epub")) == 3000 if files(r.sd, "offline/Alpha.epub") else False,
        "Alpha.epub is on the card, whole",
    )
    r.expect(files(r.sd, "offline/Beta.fb2") != [], "Beta.fb2 is on the card")
    r.expect(files(r.sd, "offline/*.part") == [], "no .part is left")
    r.expect(read(r.sd, "offline/Alpha.epub") == b"b" * 3000, "the content is what was sent")
    return r


@case("books: a book that is there is not replaced unless asked")
def _():
    def setup(sd):
        put(sd, "offline/Alpha.epub", b"OLD" * 100)

    r = run([post("/api/upload?sizes=500", files=[book("Alpha.epub", 500)])], setup)
    d = r.json(0) or {}
    r.expect((d.get("files") or [{}])[0].get("exists") is True, f"the answer says it exists: {d}")
    r.expect(read(r.sd, "offline/Alpha.epub") == b"OLD" * 100, "the old copy is untouched")
    r.expect(files(r.sd, "offline/*.part") == [], "no .part is left")
    return r


@case("books: with overwrite the new copy replaces it")
def _():
    def setup(sd):
        put(sd, "offline/Alpha.epub", b"OLD" * 100)

    r = run([post("/api/upload?overwrite=1&sizes=500", files=[book("Alpha.epub", 500)])], setup)
    r.expect(read(r.sd, "offline/Alpha.epub") == b"b" * 500, "the new copy is on the card")
    r.expect(((r.json(0) or {}).get("files") or [{}])[0].get("ok") is True, "the answer says it is stored")
    return r


@case("books: overwriting drops the reading progress, unless it is asked to keep it")
def _():
    progress = {"version": 1, "books": [{"path": "/offline/Alpha.epub", "title": "Alpha", "local": {"p": 0.5}}]}

    def setup(sd):
        put(sd, "offline/Alpha.epub", b"OLD" * 100)
        put(sd, "books/progress.json", json.dumps(progress))

    dropped = run([post("/api/upload?overwrite=1&keep_progress=0&sizes=500", files=[book("Alpha.epub", 500)])], setup)
    kept = run([post("/api/upload?overwrite=1&keep_progress=1&sizes=500", files=[book("Alpha.epub", 500)])], setup)

    def has(r):
        path = card(r.sd, "books/progress.json")
        return os.path.exists(path) and "/offline/Alpha.epub" in open(path).read()

    dropped.expect(not has(dropped), "the progress goes with the old copy")
    kept.expect(has(kept), "keep_progress=1 keeps the progress")
    dropped.problems += kept.problems
    return dropped


@case("books: names that are not books, or lead out of the folder, are refused")
def _():
    r = run(
        [
            post(
                "/api/upload?sizes=10,10,10,10",
                files=[
                    book("virus.exe", 10),
                    book("../../escape.epub", 10),
                    book(".hidden.epub", 10),
                    book("ok.epub", 10),
                ],
            ),
        ]
    )
    d = (r.json(0) or {}).get("files", [])
    r.expect(
        len(d) == 4 and [x.get("ok") is True for x in d] == [False, True, False, True], f"only the book is taken: {d}"
    )
    r.expect(
        files(r.sd, "offline/*.exe") == []
        and files(r.sd, "escape.epub") == []
        and files(r.sd, "offline/.hidden*") == [],
        "nothing else is written",
    )
    r.expect(
        files(r.sd, "offline/escape.epub") != [] or files(r.sd, "offline/ok.epub") != [],
        "a path in the name is cut to its file name",
    )
    return r


@case("books: a request without a file is a bad request")
def _():
    r = run([post("/api/upload")])
    r.expect(r.code(0) == 400, f"answered 400, not {r.code(0)}")
    return r


@case("books: a book is deleted with its cache and cover; others stay")
def _():
    def setup(sd):
        put(sd, "offline/Alpha.epub", b"a" * 100)
        put(sd, "offline/Beta.epub", b"b" * 100)

    r = run(
        [
            post("/api/books/delete", {"name": "Alpha.epub"}),
            post("/api/books/delete", {"name": "Alpha.epub"}),
            post("/api/books/delete", {"name": "../books/x.epub"}),
        ],
        setup,
    )
    r.expect(r.code(0) == 200 and (r.json(0) or {}).get("ok") is True, "the delete is answered ok")
    r.expect(
        files(r.sd, "offline/Alpha.epub") == [] and files(r.sd, "offline/Beta.epub") != [], "only that book is gone"
    )
    r.expect(r.code(1) == 404, f"a book that is not there is a 404, not {r.code(1)}")
    r.expect(r.code(2) in (400, 404), f"a path out of the folder is refused, not {r.code(2)}")
    return r


# ---- fonts
@case("fonts: a font upload goes to its family's folder")
def _():
    r = run([post("/api/upload?kind=font&sizes=2000", files=[font("Alegreya-Regular.ttf")])], with_fonts())
    r.expect(((r.json(0) or {}).get("files") or [{}])[0].get("ok") is True, f"stored: {r.json(0)}")
    r.expect(files(r.sd, "fonts/Alegreya*/Alegreya-Regular.ttf") != [], "it is in /fonts/<family>/")
    r.expect(
        files(
            r.sd,
            "fonts/**/*.part",
        )
        == []
        and files(r.sd, "fonts/*/*.part") == [],
        "no .part is left",
    )
    return r


@case("fonts: two families with the same file name do not collide")
def _():
    r = run(
        [
            post("/api/upload?kind=font&sizes=2000", files=[font("Alpha-Regular.ttf")]),
            post("/api/upload?kind=font&sizes=2100", files=[font("Beta-Regular.ttf", 2100)]),
        ],
        with_fonts(),
    )
    r.expect(
        files(r.sd, "fonts/Alpha*/Alpha-Regular.ttf") != [] and files(r.sd, "fonts/Beta*/Beta-Regular.ttf") != [],
        "each family has its own folder",
    )
    return r


@case("fonts: a file that is not a font, or not whole, is refused and leaves nothing")
def _():
    r = run(
        [
            post("/api/upload?kind=font&sizes=2000", files=[font("Bad-Regular.ttf", 2000, "deadbeef")]),
            post("/api/upload?kind=font&sizes=3000", files=[font("Short-Regular.ttf", 2000)]),
            post("/api/upload?kind=font&sizes=100", files=[font("notafont.txt", 100)]),
        ],
        with_fonts(),
    )
    for i, why in enumerate(["bad signature", "size differs from the declared", "not a font file name"]):
        f = ((r.json(i) or {}).get("files") or [{}])[0]
        r.expect(f.get("ok") is not True, f"refused ({why}): {f}")
    r.expect(
        files(r.sd, "fonts/*/*.ttf") == []
        and files(r.sd, "fonts/*.part") == []
        and files(r.sd, "fonts/*/*.part") == [],
        "nothing is left on the card",
    )
    return r


@case("fonts: the built-in font is not uploaded")
def _():
    r = run(
        [get("/api/fonts"), post("/api/upload?kind=font&sizes=2000", files=[font("NotoSerif-Regular.ttf")])],
        with_fonts(),
    )
    builtin = (r.json(0) or {}).get("builtin", "Noto Serif").replace(" ", "")
    f = ((r.json(1) or {}).get("files") or [{}])[0]
    r.expect(f.get("ok") is not True, f"refused: {f} (built-in: {builtin})")
    return r


@case("fonts: a font on the card is deleted with its whole family")
def _():
    def setup(sd):
        with_fonts("Literata", "NotoSans")(sd)
        # a second face of the family, in a folder of its own: a real font file, or it is not one
        os.makedirs(card(sd, "fonts/Literata"), exist_ok=True)
        shutil.copy(
            os.path.join(ROOT, "fonts", "sd", "Literata", "Literata-Italic.ttf"),
            card(sd, "fonts/Literata/Literata-Italic.ttf"),
        )

    r = run([post("/api/fonts/card/delete", {"name": "Literata"})], setup)
    d = r.json(0) or {}
    r.expect(r.code(0) == 200 and d.get("ok") is True and d.get("inFlash") is False, f"deleted: {d}")
    r.expect(font_files(r.sd, "Literata") == [], f"every file of the family is gone: {font_files(r.sd, 'Literata')}")
    r.expect(files(r.sd, "fonts/NotoSans*") != [], "the other family stays")
    return r


@case("fonts: deleting the reading font from the card clears the choice")
def _():
    r = run(
        [post("/api/fonts/card/delete", {"name": "Literata"})],
        with_fonts("Literata", "NotoSans"),
        config={"font_family": "Literata"},
    )
    r.expect(cfg(r.sd).get("font_family", "") == "", "the reading font is no longer set")
    keep = run(
        [post("/api/fonts/card/delete", {"name": "NotoSans"})],
        with_fonts("Literata", "NotoSans"),
        config={"font_family": "Literata"},
    )
    keep.expect(cfg(keep.sd).get("font_family") == "Literata", "deleting another font keeps the choice")
    r.problems += keep.problems
    return r


@case("fonts: the card delete refuses what it cannot do")
def _():
    r = run(
        [
            post("/api/fonts/card/delete", {"name": "Nothing"}),
            post("/api/fonts/card/delete", {"name": "Noto Serif"}),
            post("/api/fonts/card/delete"),
        ],
        with_fonts("Literata"),
    )
    r.expect(r.code(0) == 404, f"a family that is not there is a 404, not {r.code(0)}")
    r.expect(r.code(1) == 409, f"the built-in font is a 409, not {r.code(1)}")
    r.expect(r.code(2) in (400, 404), f"no name is refused, not {r.code(2)}")
    r.expect(files(r.sd, "fonts/Literata*") != [], "nothing was deleted")
    return r


@case("fonts: a font is installed in Flash, becomes the reading font, and is deleted again")
def _():
    r = run(
        [
            post("/api/fonts/install", {"name": "Literata"}),
            get("/api/fonts"),
            post("/api/fonts/flash/delete", {"name": "Literata"}),
            get("/api/fonts"),
        ],
        with_fonts("Literata", "NotoSans"),
    )
    r.expect(r.code(0) == 200 and (r.json(0) or {}).get("ok") is True, f"installed: {r.json(0)}")
    r.expect(
        [f["name"] for f in (r.json(1) or {}).get("installedFamilies", [])] == ["Literata"], "the Flash list has it"
    )
    r.expect(r.code(2) == 200 and (r.json(2) or {}).get("ok") is True, f"deleted from Flash: {r.json(2)}")
    r.expect((r.json(3) or {}).get("installedFamilies") == [], "the Flash list is empty again")
    r.expect(cfg(r.sd).get("font_family", "") == "", "deleting the reading font from Flash clears the choice")
    r.expect(files(r.sd, "fonts/Literata*") != [], "the card copy stays")
    return r


@case("fonts: the install and the Flash delete refuse what they cannot do")
def _():
    r = run(
        [
            post("/api/fonts/install", {"name": "Nothing"}),
            post("/api/fonts/install", {"name": "Noto Serif"}),
            post("/api/fonts/flash/delete", {"name": "Literata"}),
            post("/api/fonts/flash/delete", {"name": "Noto Serif"}),
        ],
        with_fonts("Literata"),
    )
    r.expect(r.code(0) in (400, 404), f"a family that is not on the card is refused, not {r.code(0)}")
    r.expect(r.code(1) == 409, f"installing the built-in font is a 409, not {r.code(1)}")
    r.expect(r.code(2) == 404, f"deleting what is not in Flash is a 404, not {r.code(2)}")
    r.expect(r.code(3) == 409, f"deleting the built-in font is a 409, not {r.code(3)}")
    return r


# ---- status bars
@case("status bars: every portal checkbox round-trips independently")
def _():
    requests = []
    for bit in (1 << i for i in range(11)):
        requests.extend(
            [post("/save?ajax=1", {"sb_top_items_present": "1", f"sbt_{bit}": "1", f"sbb_{bit}": "1"}), get("/")]
        )
    r = run(requests)
    for i in range(11):
        r.expect(r.code(i * 2) == 200, "saving a single status-bar item succeeds")
        html = r.resp[i * 2 + 1]["body"]
        for prefix in ("sbt", "sbb"):
            for j in range(11):
                tag = re.search(rf"<input[^>]*name='{prefix}_{1 << j}'[^>]*>", html)
                r.expect(
                    tag is not None and (" checked" in tag[0]) == (i == j),
                    f"{prefix} bit {j}: checkbox follows mask {1 << i}",
                )
    c = cfg(r.sd)
    r.expect(c.get("sb_top_items") == 1024 and c.get("sb_bottom_items") == 1024, "high bits survive persistence")
    return r


@case("status bars: clearing all portal checkboxes hides both bars")
def _():
    r = run(
        [post("/save?ajax=1", {"sb_top_items_present": "1"}), get("/")],
        config={"sb_top_items": 2047, "sb_bottom_items": 2047},
    )
    c = cfg(r.sd)
    r.expect(c.get("sb_top_items") == 0 and c.get("sb_bottom_items") == 0, "all unchecked really means no items")
    for tag in re.findall(r"<input[^>]*name='sb[tb]_\d+'[^>]*>", r.resp[1]["body"]):
        r.expect(" checked" not in tag, "reloaded form keeps every checkbox off")
    return r


# ---- settings
@case("settings: Save changes the settings and answers ok")
def _():
    form = {
        "wifi_ssid": "NewNet",
        "wifi_pass": "secret-pass",
        "server_url": "books.example:3000",
        "username": "alice",
        "sync_on_boot": "1",
        "lang": "ru",
    }
    r = run([post("/save?ajax=1", form)])
    c = cfg(r.sd)
    r.expect(r.code(0) == 200 and (r.json(0) or {}).get("ok") is True, f"answered ok: {r.json(0)}")
    r.expect(
        c.get("wifi_ssid") == "NewNet" and c.get("wifi_pass") == "secret-pass",
        f"the Wi-Fi is saved: {c.get('wifi_ssid')}",
    )
    r.expect(c.get("server_url") == "http://books.example:3000", f"the server gets its scheme: {c.get('server_url')}")
    r.expect(c.get("username") == "alice" and c.get("sync_on_boot") is True, "the user and a checked box are saved")
    return r


@case("settings: a box left unchecked is saved as off, an empty password keeps the saved one")
def _():
    r = run(
        [post("/save?ajax=1", {"wifi_ssid": "TestNetwork", "wifi_pass": "", "password": ""})],
        config={"sync_on_boot": True},
    )
    c = cfg(r.sd)
    r.expect(c.get("sync_on_boot") is False, "an unchecked box is off")
    r.expect(
        c.get("wifi_pass") == "test-password", f"an empty Wi-Fi password keeps the saved one: {c.get('wifi_pass')}"
    )
    return r


@case("settings: Save without ajax answers the Saved page")
def _():
    r = run([post("/save", {"wifi_ssid": "TestNetwork"})])
    r.expect(
        r.code(0) == 200 and r.resp[0]["type"].startswith("text/html") and "<html" in r.resp[0]["body"],
        "an HTML page answers",
    )
    return r


@case("close: the portal closes on Close and says so")
def _():
    r = run([])
    r.expect(r.code(0) == 200 and (r.json(0) or {}).get("ok") is True, "Close is answered ok")
    r.expect("Web Configuration Portal closed" in r.log, "the portal's loop ended")
    return r


# ---- the PIN (home Wi-Fi)
PROTECTED = [
    ("GET", "/"),
    ("GET", "/api/books"),
    ("GET", "/api/fonts"),
    ("POST", "/api/upload"),
    ("POST", "/api/books/delete"),
    ("POST", "/api/fonts/card/delete"),
    ("POST", "/api/fonts/flash/delete"),
    ("POST", "/api/fonts/install"),
    ("POST", "/save"),
    ("POST", "/api/close"),
]


@case("PIN: nothing answers without the PIN on a home Wi-Fi portal")
def _():
    def setup(sd):
        with_fonts("Literata")(sd)
        put(sd, "offline/Keep.epub", b"k" * 100)

    reqs = [get(u) if m == "GET" else post(u, {"name": "Keep.epub"}) for m, u in PROTECTED]
    r = run(reqs, setup, script=STA)  # (the run closes the portal itself, with the PIN, after these)
    for i, (m, u) in enumerate(PROTECTED):
        r.expect(r.code(i) == 401, f"{m} {u} without a PIN answers 401, not {r.code(i)}")
    r.expect(files(r.sd, "offline/Keep.epub") != [] and files(r.sd, "fonts/Literata*") != [], "nothing was deleted")
    return r


@case("PIN: a wrong PIN is refused, the right one is taken from the address or from X-PIN")
def _():
    r = run(
        [
            get("/api/books?pin=000000"),
            get("/api/books?pin=" + PIN),
            get("/api/books", {"X-PIN": PIN}),
            get("/api/books", {"X-PIN": "1"}),
            post(pin("/api/close")),
        ],
        script=STA,
        own_close=True,
    )
    r.expect([r.code(i) for i in range(5)] == [401, 200, 200, 401, 200], f"answers: {[r.code(i) for i in range(5)]}")
    return r


@case("PIN: every action works with the PIN")
def _():
    def setup(sd):
        with_fonts("Literata", "NotoSans")(sd)
        put(sd, "offline/Old.epub", b"o" * 100)

    r = run(
        [
            get(pin("/")),
            post(pin("/api/upload?sizes=300"), files=[book("New.epub", 300)]),
            post(pin("/api/books/delete"), {"name": "Old.epub"}),
            post(pin("/api/fonts/card/delete"), {"name": "NotoSans"}),
            post(pin("/save?ajax=1"), {"wifi_ssid": "TestNetwork"}),
            post(pin("/api/close")),
        ],
        setup,
        script=STA,
        own_close=True,
    )
    r.expect([r.code(i) for i in range(6)] == [200] * 6, f"answers: {[r.code(i) for i in range(6)]}")
    r.expect(
        files(r.sd, "offline/New.epub") != []
        and files(r.sd, "offline/Old.epub") == []
        and files(r.sd, "fonts/NotoSans*") == [],
        "the actions took effect",
    )
    return r


@case("PIN: the access point portal asks for none")
def _():
    r = run([get("/api/books"), post("/api/books/delete", {"name": "x.epub"})])
    r.expect(r.code(0) == 200 and r.code(1) == 404, f"answered, not 401: {r.code(0)}, {r.code(1)}")
    return r


# ---------------------------------------------------------------- the runner
def run_case(case_):
    name, fn = case_
    try:
        r = fn()
        return name, r.problems, r
    except Exception as e:  # a broken case fails itself, not the run
        return name, [f"raised {e!r}"], None


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    cases = [c for c in CASES if only in c[0]]
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(run_case, cases))
    failed = 0
    for name, problems, r in results:
        print(("ok    " if not problems else "FAIL  ") + name + ("" if not problems else "  -> " + "; ".join(problems)))
        if problems:
            failed += 1
            if os.environ.get("VERBOSE") and r:
                for a in r.resp:
                    print("      ", a.get("code"), a.get("uri"), str(a.get("body"))[:300])
        if r:
            shutil.rmtree(r.sd, ignore_errors=True)
    print(f"{'OK' if not failed else 'FAILED'}: {len(cases) - failed}/{len(cases)} portal server cases passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
