#!/usr/bin/env python3
"""Layer rules of the firmware sources (src/). Exit 1 and list the breaches.

1. Which board or chip this is, is the platform's business: the build's
   device and capability flags (FREEINK_DEVICE_*, FREEINK_CAP_*,
   FREEINK_GT911_*, BOARD_HAS_PSRAM) and ESP_PLATFORM are tested only in
   src/platform. Everything else reads Platform::kDevice / kMemory.
2. src/core is pure logic: it includes only src/core.
3. src/platform includes only src/platform and src/core.

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
                        problems.append(f"{rel}:{n}: {m.group(1)} outside src/platform (use Platform::kDevice / kMemory)")

            allowed = ALLOWED_INCLUDES.get(layer)
            if allowed is None:
                continue
            for m in INCLUDE.finditer(code):
                target = os.path.normpath(os.path.join(d, m.group(1)))
                if not target.startswith(os.path.normpath(SRC) + os.sep):
                    continue   # include/, the SDK, system headers
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
