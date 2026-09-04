#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Post-assembly gate for the browser web-installer site (#196).

Not a PlatformIO extra_script - a plain CLI tool, same shape as
check_version_code.py / assemble_site.py. Run right after
build-scripts/assemble_site.py, in both .github/workflows/test.yml's
web-installer-assemble job and .github/workflows/pages.yml's real deploy.

assemble_site.py validates its *inputs* (offsets, NVS overlap, chipFamily
spelling) but nothing then inspects the manifest.json / version.json it
*wrote*. A path or board-list regression that still satisfies assemble_site's
own checks would ship to the deploy with CI green. This reads the assembled
site back and fails loudly if:

  - manifest.json doesn't describe exactly the 3 expected chip families
  - version.json doesn't describe the same 3 boards
  - any part path either manifest references is missing or empty on disk
"""

import argparse
import json
import sys
from pathlib import Path

EXPECTED_CHIP_FAMILIES = {"ESP32", "ESP32-S3", "ESP32-C3"}
EXPECTED_BOARDS = {"esp32_wroom", "esp32_s3_zero", "esp32_c3_zero"}


def _fail(msg: str):
    sys.exit(f"check_site: {msg}")


def _load(site: Path, name: str) -> dict:
    path = site / name
    if not path.is_file():
        _fail(f"{path} is missing - did assemble_site.py run?")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        _fail(f"{path} is not valid JSON: {e}")


def check_site(site: Path):
    manifest = _load(site, "manifest.json")
    version = _load(site, "version.json")

    builds = manifest.get("builds", [])
    families = [b.get("chipFamily") for b in builds]
    if len(builds) != 3:
        _fail(f"manifest.json has {len(builds)} builds, expected 3")
    if set(families) != EXPECTED_CHIP_FAMILIES:
        _fail(f"manifest.json chipFamily set is {sorted(f for f in families if f)}, "
              f"expected {sorted(EXPECTED_CHIP_FAMILIES)}")

    for build in builds:
        parts = build.get("parts", [])
        if not parts:
            _fail(f"manifest.json build {build.get('chipFamily')!r} has no parts")
        for part in parts:
            rel = part.get("path")
            if not rel:
                _fail(f"manifest.json build {build.get('chipFamily')!r} has a part with no path")
            f = site / rel
            if not f.is_file() or f.stat().st_size == 0:
                _fail(f"manifest.json references {rel!r} but {f} is missing or empty")

    boards = version.get("boards", [])
    board_names = {b.get("board") for b in boards}
    if board_names != EXPECTED_BOARDS:
        _fail(f"version.json boards are {sorted(n for n in board_names if n)}, "
              f"expected {sorted(EXPECTED_BOARDS)}")
    if {b.get("chipFamily") for b in boards} != EXPECTED_CHIP_FAMILIES:
        _fail("version.json chipFamily set disagrees with manifest.json")

    print(f"check_site: OK - {site}/manifest.json + version.json describe all 3 boards, "
          "every part file present")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--site", default="_site", type=Path, help="assembled site directory")
    args = ap.parse_args()
    if not args.site.is_dir():
        _fail(f"{args.site} is not a directory")
    check_site(args.site)


if __name__ == "__main__":
    main()
