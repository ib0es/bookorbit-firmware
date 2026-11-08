"""Apply the Book Orbit patches in patches/freeink-sdk/ to the freeink-sdk submodule.

Runs before every PlatformIO build (extra_scripts = pre:...) and can be run by
hand: python3 tools/apply_sdk_patches.py. Idempotent: a patch that is already
applied is skipped; one that applies neither way stops the build, because the
firmware would compile but misbehave on the hardware (e.g. the SD card
dropping off the bus shared with the display on X4/X3).
"""
import glob
import os
import subprocess
import sys

try:
    Import("env")  # noqa: F821  (PlatformIO SCons)
    ROOT = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SDK = os.path.join(ROOT, "freeink-sdk")
PATCHES = sorted(glob.glob(os.path.join(ROOT, "patches", "freeink-sdk", "*.patch")))


def git_apply(*args):
    return subprocess.run(["git", "apply", *args], cwd=SDK, capture_output=True, text=True)


def main():
    if not os.path.isdir(os.path.join(SDK, "libs")):
        sys.exit("freeink-sdk is missing: run `git submodule update --init`")
    for patch in PATCHES:
        name = os.path.basename(patch)
        if git_apply("--reverse", "--check", patch).returncode == 0:
            continue  # already applied
        res = git_apply("--check", patch)
        if res.returncode != 0:
            sys.exit(f"[sdk-patches] {name} does not apply to freeink-sdk "
                     f"(the submodule moved?):\n{res.stderr}")
        res = git_apply(patch)
        if res.returncode != 0:
            sys.exit(f"[sdk-patches] applying {name} failed:\n{res.stderr}")
        print(f"[sdk-patches] applied {name}")


main()
