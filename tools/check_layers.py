#!/usr/bin/env python3
"""Layer rules of the firmware sources (src/). Exit 1 and list the breaches.

1. Which board or chip this is, is the platform's business: the build's
   device and capability flags (FREEINK_DEVICE_*, FREEINK_CAP_*,
   FREEINK_GT911_*, BOARD_HAS_PSRAM) and ESP_PLATFORM are tested only in
   src/platform. Everything else reads Platform::kDevice / kMemory.
2. src/core is pure logic: it includes only src/core.
3. src/platform includes only src/platform and src/core.
4. The layers below the UI (reader, convert, net, storage, services) do not
   include src/app or src/ui: they ask the screen through core/ui_port.h.
   Shared exceptions: the app's state and settings (app/app_state.h) and
   text, not drawing (ui/i18n.h, ui/text_utils.h, ui/wifi_messages.h, the
   book fonts ui/font.h). KNOWN_EXCEPTIONS lists what is left to move; a
   new breach fails.

Comments are ignored. Run from anywhere: python3 tools/check_layers.py
"""

import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src")
EXT = (".h", ".hpp", ".c", ".cpp")

FLAG = re.compile(r"\b(FREEINK_DEVICE_\w+|FREEINK_CAP_\w+|FREEINK_GT911_\w+|BOARD_HAS_PSRAM|ESP_PLATFORM)\b")
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)

# Which src/ top-level directories a directory may include from (None: any).
ALLOWED_INCLUDES = {
    "core": {"core"},
    "platform": {"platform", "core"},
}

LOWER_LAYERS = {"reader", "convert", "net", "storage", "services"}
SHARED_UPPER = {
    "app/app_state.h",
    "ui/i18n.h",
    "ui/i18n_generated.h",
    "ui/text_utils.h",
    "ui/wifi_messages.h",
    "ui/font.h",
}
# (file, included) pairs still to be moved; see the fix plan (A8).
KNOWN_EXCEPTIONS = {
    ("src/reader/book_reader_render.cpp", "ui/kit/ui_kit.h"),  # the page-problem notice, turning the UI target
    ("src/reader/book_reader_preview.cpp", "ui/kit/ui_kit.h"),  # the Appearance preview turns the UI target
}


def strip_comments(text):
    """Comments out, line structure kept (so line numbers still match)."""

    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', re.S)
    return pattern.sub(lambda m: m.group(0) if m.group(0).startswith('"') else blank(m), text)


def top_dir(path):
    rel = os.path.relpath(path, SRC)
    return rel.split(os.sep)[0] if os.sep in rel else ""


def main():
    problems = []
    for d, _, files in os.walk(SRC):
        for f in sorted(files):
            if not f.endswith(EXT):
                continue
            path = os.path.join(d, f)
            rel = os.path.relpath(path, ROOT)
            layer = top_dir(path)
            with open(path, encoding="utf-8", errors="replace") as fh:
                code = strip_comments(fh.read())

            if layer != "platform":
                for n, line in enumerate(code.split("\n"), 1):
                    m = FLAG.search(line)
                    if m:
                        problems.append(
                            f"{rel}:{n}: {m.group(1)} outside src/platform (use Platform::kDevice / kMemory)"
                        )

            if layer in LOWER_LAYERS:
                for m in INCLUDE.finditer(code):
                    target = os.path.normpath(os.path.join(d, m.group(1)))
                    if not os.path.exists(target):
                        target = os.path.normpath(os.path.join(SRC, m.group(1)))  # an include through -Isrc
                    if not target.startswith(os.path.normpath(SRC) + os.sep):
                        continue
                    to = top_dir(target)
                    inc = os.path.relpath(target, SRC).replace(os.sep, "/")
                    if to in ("app", "ui") and inc not in SHARED_UPPER and (rel, inc) not in KNOWN_EXCEPTIONS:
                        n = code.count("\n", 0, m.start()) + 1
                        problems.append(f"{rel}:{n}: src/{layer} includes {inc} (go through core/ui_port.h)")

            allowed = ALLOWED_INCLUDES.get(layer)
            if allowed is None:
                continue
            for m in INCLUDE.finditer(code):
                target = os.path.normpath(os.path.join(d, m.group(1)))
                if not target.startswith(os.path.normpath(SRC) + os.sep):
                    continue  # include/, the SDK, system headers
                to = top_dir(target)
                if to and to not in allowed:
                    n = code.count("\n", 0, m.start()) + 1
                    problems.append(f"{rel}:{n}: src/{layer} includes src/{to} ({m.group(1)})")

    for p in problems:
        print(p)
    print(f"{'FAILED' if problems else 'OK'}: layer rules, {len(problems)} breach(es)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
