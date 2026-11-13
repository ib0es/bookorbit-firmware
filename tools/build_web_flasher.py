#!/usr/bin/env python3
"""
Book Orbit — Web Flasher Build & Packaging Tool.

Compiles firmware targets (or uses existing .pio build artifacts),
merges bootloader, partition table, otadata (boot_app0), and application
into a single monolithic binary for each device, and prepares manifests
and assets for GitHub Pages / ESP Web Tools.

Usage:
    python3 tools/build_web_flasher.py                 # build all targets
    python3 tools/build_web_flasher.py --targets x4    # build specific target(s)
    python3 tools/build_web_flasher.py --skip-build    # pack already-built binaries
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# Map environment -> (chip_family, display_name, description)
TARGETS = {
    "x4": {
        "chip": "esp32c3",
        "name": "Book Orbit for Xteink X4",
        "device": "Xteink X4",
        "desc": "ESP32-C3, 6\" E-Ink 800x600, 5 navigation buttons",
    },
    "x3": {
        "chip": "esp32c3",
        "name": "Book Orbit for Xteink X3",
        "device": "Xteink X3",
        "desc": "ESP32-C3, 792x528 UC8253 / UC8279, 5 navigation buttons",
    },
    "x4c": {
        "chip": "esp32s3",
        "name": "Book Orbit for Xteink X4 Classic",
        "device": "Xteink X4 Classic",
        "desc": "ESP32-S3 (8MB PSRAM), SSD1677/UC8179/UC8279, 8 buttons (no touch/light), USB-MSC, BM8563 RTC, QMI8658 IMU",
    },
    "x4pro": {
        "chip": "esp32s3",
        "name": "Book Orbit for Xteink X4 Pro",
        "device": "Xteink X4 Pro",
        "desc": "ESP32-S3 (8MB PSRAM), GT911 touch + Home key, dual frontlight, USB-MSC, BM8563 RTC",
    },
    "picco": {
        "chip": "esp32s3",
        "name": "Book Orbit for BOOX Picco",
        "device": "BOOX Picco / Tiles",
        "desc": "ESP32-S3 (8MB PSRAM), TMA525C touch + I2C frontlight",
    },
}

REPO_ROOT = Path(__file__).resolve().parent.parent
PIO_BUILD_DIR = REPO_ROOT / ".pio" / "build"
DOCS_DIR = REPO_ROOT / "docs"
BUILDS_DIR = DOCS_DIR / "builds"
MANIFESTS_DIR = DOCS_DIR / "manifests"


def find_boot_app0() -> Path:
    """Locate boot_app0.bin in repo tools or PlatformIO Arduino package."""
    local_tool = REPO_ROOT / "tools" / "boot_app0.bin"
    if local_tool.is_file():
        return local_tool

    home = Path.home()
    pio_pkg = (
        home / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
    )
    if pio_pkg.is_file():
        # Copy locally for reproducibility
        try:
            shutil.copy2(pio_pkg, local_tool)
        except Exception:
            pass
        return pio_pkg

    # Search anywhere in ~/.platformio
    for match in (home / ".platformio").glob("**/boot_app0.bin"):
        if match.is_file():
            return match

    raise FileNotFoundError("Could not locate boot_app0.bin. Please compile with PlatformIO first.")


def run_cmd(cmd, cwd=None):
    print(f"-> {' '.join(str(c) for c in cmd)}")
    res = subprocess.run(cmd, cwd=cwd or REPO_ROOT)
    if res.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {res.returncode}: {' '.join(str(c) for c in cmd)}")


def build_env(env: str):
    print("\n==================================================")
    print(f"Building PlatformIO environment: {env}")
    print("==================================================")
    run_cmd(["pio", "run", "-e", env])


def merge_firmware(env: str, chip: str, boot_app0_path: Path):
    env_dir = PIO_BUILD_DIR / env
    bootloader = env_dir / "bootloader.bin"
    partitions = env_dir / "partitions.bin"
    firmware = env_dir / "firmware.bin"

    for f in (bootloader, partitions, firmware):
        if not f.is_file():
            raise FileNotFoundError(f"Missing build artifact: {f}. Run build first.")

    out_env_dir = BUILDS_DIR / env
    out_env_dir.mkdir(parents=True, exist_ok=True)

    firmware_out = out_env_dir / f"bookorbit-{env}.bin"

    print(f"Merging binaries for {env} ({chip})...")
    # Offsets for ESP32-C3 and ESP32-S3 with default_16MB partition layout:
    # 0x0000: bootloader
    # 0x8000: partitions
    # 0xe000: boot_app0 (otadata)
    # 0x10000: app (firmware.bin)
    merge_cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        chip,
        "merge_bin",
        "-o",
        str(firmware_out),
        "--flash_mode",
        "dio",
        "--flash_size",
        "16MB",
        "0x0",
        str(bootloader),
        "0x8000",
        str(partitions),
        "0xe000",
        str(boot_app0_path),
        "0x10000",
        str(firmware),
    ]
    run_cmd(merge_cmd)

    size_mb = firmware_out.stat().st_size / (1024 * 1024)
    print(f"✓ Created {firmware_out.name} ({size_mb:.2f} MB)")


def generate_manifests(targets):
    import json

    MANIFESTS_DIR.mkdir(parents=True, exist_ok=True)

    for env in targets:
        info = TARGETS[env]
        chip_family = "ESP32-C3" if info["chip"] == "esp32c3" else "ESP32-S3"
        manifest_data = {
            "name": info["name"],
            "version": "latest",
            "new_install_prompt_erase": True,
            "builds": [
                {"chipFamily": chip_family, "parts": [{"path": f"../builds/{env}/bookorbit-{env}.bin", "offset": 0}]}
            ],
        }
        manifest_file = MANIFESTS_DIR / f"{env}.json"
        with open(manifest_file, "w", encoding="utf-8") as f:
            json.dump(manifest_data, f, indent=2)
        print(f"✓ Generated manifest: {manifest_file.relative_to(REPO_ROOT)}")

    # Also generate the root universal auto-detect manifest
    universal_manifest = {
        "name": "Book Orbit (Auto-detect)",
        "version": "latest",
        "new_install_prompt_erase": True,
        "builds": [
            {"chipFamily": "ESP32-C3", "parts": [{"path": "builds/x4/bookorbit-x4.bin", "offset": 0}]},
            {"chipFamily": "ESP32-S3", "parts": [{"path": "builds/x4c/bookorbit-x4c.bin", "offset": 0}]},
        ],
    }
    with open(DOCS_DIR / "manifest.json", "w", encoding="utf-8") as f:
        json.dump(universal_manifest, f, indent=2)
    print("✓ Generated universal manifest: docs/manifest.json")


def main():
    parser = argparse.ArgumentParser(description="Book Orbit Web Flasher Packager")
    parser.add_argument(
        "--targets",
        nargs="+",
        choices=list(TARGETS.keys()),
        default=list(TARGETS.keys()),
        help="Target environments to process",
    )
    parser.add_argument("--skip-build", action="store_true", help="Skip compilation, only merge existing artifacts")
    parser.add_argument(
        "--manifests-only", action="store_true", help="Only generate manifest JSON files without compiling or merging"
    )
    args = parser.parse_args()

    if args.manifests_only:
        generate_manifests(args.targets)
        print("\n✓ Manifests generated successfully!")
        return

    boot_app0 = find_boot_app0()
    print(f"Found boot_app0: {boot_app0}")

    for target in args.targets:
        if not args.skip_build:
            build_env(target)
        merge_firmware(target, TARGETS[target]["chip"], boot_app0)

    generate_manifests(args.targets)
    print("\n✓ Web Flasher packaging complete!")


if __name__ == "__main__":
    main()
