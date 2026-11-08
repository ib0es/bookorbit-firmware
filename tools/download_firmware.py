#!/usr/bin/env python3
"""
Book Orbit — Firmware Download Tool.

Download pre-compiled release or latest master builds for any supported device
directly from GitHub.

Usage:
    python3 tools/download_firmware.py x4
    python3 tools/download_firmware.py picco --channel release
    python3 tools/download_firmware.py x3 --channel latest --type app
    python3 tools/download_firmware.py x4pro --tag v0.1.0 --output-dir ./firmware_bins
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_REPO = "ib0es/bookorbit-firmware"

DEVICES = {
    "x4": "Xteink X4",
    "x3": "Xteink X3",
    "x4c": "Xteink X4 Classic",
    "x4pro": "Xteink X4 Pro",
    "picco": "BOOX Picco / Tiles",
}


def format_bytes(size: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024.0:
            return f"{size:.1f} {unit}" if unit != "B" else f"{size} {unit}"
        size /= 1024.0
    return f"{size:.1f} GB"


def download_file(url: str, dest_path: Path):
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = dest_path.with_suffix(dest_path.suffix + ".part")

    print(f"Downloading: {url}")
    print(f"Destination: {dest_path}")

    req = urllib.request.Request(
        url,
        headers={"User-Agent": "BookOrbit-Firmware-Downloader/1.0"}
    )

    try:
        with urllib.request.urlopen(req) as resp, open(tmp_path, "wb") as out_file:
            total_size = resp.getheader("Content-Length")
            total_size = int(total_size) if total_size and total_size.isdigit() else None
            downloaded = 0
            block_size = 64 * 1024

            while True:
                chunk = resp.read(block_size)
                if not chunk:
                    break
                out_file.write(chunk)
                downloaded += len(chunk)

                if total_size:
                    percent = downloaded * 100.0 / total_size
                    print(
                        f"\r  [{downloaded * 100 // total_size:3d}%] "
                        f"{format_bytes(downloaded)} / {format_bytes(total_size)}",
                        end="",
                        flush=True,
                    )
                else:
                    print(f"\r  {format_bytes(downloaded)} downloaded", end="", flush=True)

            print()
    except urllib.error.HTTPError as exc:
        if tmp_path.exists():
            tmp_path.unlink()
        if exc.code == 404:
            raise FileNotFoundError(f"File not found on GitHub (HTTP 404): {url}") from exc
        raise RuntimeError(f"HTTP download error ({exc.code}): {exc.reason}") from exc
    except Exception:
        if tmp_path.exists():
            tmp_path.unlink()
        raise

    if tmp_path.exists():
        tmp_path.replace(dest_path)

    size = dest_path.stat().st_size
    print(f"✓ Saved {dest_path.name} ({format_bytes(size)})")


def get_base_url(repo: str, channel: str, tag: str | None) -> str:
    if tag:
        return f"https://github.com/{repo}/releases/download/{tag}"
    if channel == "release":
        return f"https://github.com/{repo}/releases/latest/download"
    # 'latest' master channel: GitHub rolling pre-release tag 'latest'
    return f"https://github.com/{repo}/releases/download/latest"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Download Book Orbit firmware binaries for specific hardware from GitHub."
    )
    parser.add_argument(
        "device",
        choices=list(DEVICES.keys()),
        help=f"Target device identifier: {', '.join(f'{k} ({v})' for k, v in DEVICES.items())}",
    )
    parser.add_argument(
        "--channel",
        choices=["latest", "release"],
        default="latest",
        help="Build channel: 'latest' (master branch) or 'release' (latest official GitHub release). Default: latest",
    )
    parser.add_argument(
        "--tag",
        type=str,
        default=None,
        help="Download from a specific GitHub release tag (e.g. 'v0.1.0'). Overrides --channel.",
    )
    parser.add_argument(
        "--type",
        choices=["merged", "app", "all"],
        default="merged",
        help="Binary type: 'merged' (monolithic image at 0x0), 'app' (OTA application at 0x10000), or 'all'. Default: merged",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default=".",
        help="Directory to save downloaded files. Default: current directory",
    )
    parser.add_argument(
        "--repo",
        type=str,
        default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPO),
        help=f"GitHub repository in 'owner/repo' format. Default: {DEFAULT_REPO}",
    )

    args = parser.parse_args()
    device = args.device
    device_name = DEVICES[device]
    out_dir = Path(args.output_dir).resolve()

    tag_desc = args.tag or ("Latest Master Build" if args.channel == "latest" else "Latest Official Release")
    print("=" * 60)
    print(f"Book Orbit Firmware Downloader")
    print(f"Device:   {device_name} ({device})")
    print(f"Version:  {tag_desc}")
    print(f"Repo:     {args.repo}")
    print("=" * 60)

    base_url = get_base_url(args.repo, args.channel, args.tag)

    targets = []
    if args.type in ("merged", "all"):
        targets.append((f"bookorbit-{device}-merged.bin", 0x0, "Monolithic image (0x0)"))
    if args.type in ("app", "all"):
        targets.append((f"bookorbit-{device}-app.bin", 0x10000, "Application partition (0x10000 / OTA)"))

    success_files = []
    for filename, offset, desc in targets:
        url = f"{base_url}/{filename}"
        dest = out_dir / filename
        try:
            download_file(url, dest)
            success_files.append((dest, offset, desc))
        except FileNotFoundError:
            # If rolling release 'latest' is not yet seeded, fallback to GitHub Pages mirror for master
            if args.channel == "latest" and not args.tag:
                pages_url = f"https://ib0es.github.io/bookorbit-firmware/builds/{device}/{filename}"
                print(f"GitHub release asset not found. Retrying via GitHub Pages mirror...")
                try:
                    download_file(pages_url, dest)
                    success_files.append((dest, offset, desc))
                    continue
                except Exception as fallback_exc:
                    print(f"Fallback also failed: {fallback_exc}", file=sys.stderr)

            print(
                f"\nError: Could not locate {filename} on GitHub for '{tag_desc}'.\n"
                f"Make sure a release exists or that the build has completed on GitHub Actions.",
                file=sys.stderr,
            )
            return 1
        except Exception as exc:
            print(f"\nDownload failed: {exc}", file=sys.stderr)
            return 1

    print("\n" + "=" * 60)
    print("Flashing Instructions:")
    for path, offset, desc in success_files:
        print(f"• {desc}:")
        print(f"  esptool.py write_flash {hex(offset)} {path.name}")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    sys.exit(main())
