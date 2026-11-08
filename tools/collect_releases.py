#!/usr/bin/env python3
"""
Mirror recent GitHub release binaries onto GitHub Pages for the web flasher.

Browsers cannot fetch github.com release downloads (no CORS headers), so the
flasher loads release firmware from the Pages site itself. This script:
  * lists the latest releases via `gh api`,
  * downloads bookorbit-<model>-{merged,app}.bin of each into docs/builds/releases/<tag>/,
  * writes docs/releases.json with asset URLs relative to the page.

Requires the GitHub CLI with GH_TOKEN and GITHUB_REPOSITORY set (as in Actions).

Usage:
    python3 tools/collect_releases.py [--max 6]
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DOCS_DIR = REPO_ROOT / "docs"
OUT_DIR = DOCS_DIR / "builds" / "releases"
ASSET_RE = re.compile(r"^bookorbit-[a-z0-9]+-(merged|app)\.bin$")
TAG_RE = re.compile(r"^[A-Za-z0-9._-]+$")


def gh(*args: str) -> str:
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--max", type=int, default=6, help="number of releases to mirror")
    args = parser.parse_args()

    repo = os.environ.get("GITHUB_REPOSITORY")
    if not repo:
        print("GITHUB_REPOSITORY is not set", file=sys.stderr)
        return 1

    try:
        releases = json.loads(gh("api", f"repos/{repo}/releases?per_page={args.max * 2}"))
    except subprocess.CalledProcessError as exc:
        print(f"Could not list releases, writing an empty list: {exc.stderr}", file=sys.stderr)
        releases = []

    out = []
    for rel in releases:
        if rel.get("draft") or len(out) >= args.max:
            continue
        tag = rel.get("tag_name", "")
        if not TAG_RE.match(tag):
            print(f"Skipping release with unsafe tag name: {tag!r}")
            continue
        assets = [a for a in rel.get("assets", []) if ASSET_RE.match(a.get("name", ""))]
        if not assets:
            continue

        dest = OUT_DIR / tag
        dest.mkdir(parents=True, exist_ok=True)
        cmd = ["release", "download", tag, "--repo", repo, "--dir", str(dest), "--clobber"]
        for a in assets:
            cmd += ["--pattern", a["name"]]
        gh(*cmd)

        out.append({
            "tag_name": tag,
            "name": rel.get("name") or tag,
            "published_at": rel.get("published_at"),
            "prerelease": bool(rel.get("prerelease")),
            "assets": [
                {
                    "name": a["name"],
                    "browser_download_url": f"builds/releases/{tag}/{a['name']}",
                    "size": a.get("size", 0),
                }
                for a in assets
            ],
        })
        print(f"✓ {tag}: {len(assets)} file(s)")

    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    (DOCS_DIR / "releases.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"✓ Wrote docs/releases.json ({len(out)} release(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
