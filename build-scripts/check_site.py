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
  - with --require-ota: ota.json is missing, names another tag/channel, or its
    size/md5 disagree with the bytes on disk
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

EXPECTED_CHIP_FAMILIES = {"ESP32", "ESP32-S3", "ESP32-C3"}
EXPECTED_BOARDS = {"esp32_wroom", "esp32_s3_zero", "esp32_c3_zero"}
EXPECTED_OTA_BOARDS = {f"{board}_v2" for board in EXPECTED_BOARDS}


def _fail(msg: str):
    sys.exit(f"check_site: {msg}")


def _md5_and_size(path: Path):
    digest = hashlib.md5()
    size = 0
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def _load(site: Path, name: str) -> dict:
    path = site / name
    if not path.is_file():
        _fail(f"{path} is missing - did assemble_site.py run?")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        _fail(f"{path} is not valid JSON: {e}")


def _check_ota(site: Path, version: dict, require: bool, channel: str):
    path = site / "ota.json"
    if not path.is_file():
        if require:
            _fail(f"{path} is missing - assemble_site.py needs --ota-manifest/--ota-channel")
        return
    ota = _load(site, "ota.json")
    if ota.get("schema") != 1:
        _fail(f"{path}: unsupported schema {ota.get('schema')!r}")
    if ota.get("channel") != channel:
        _fail(f"{path}: channel is {ota.get('channel')!r}, expected {channel!r}")
    if ota.get("tag") != version.get("tag"):
        _fail(f"{path}: tag {ota.get('tag')!r} disagrees with version.json's "
              f"{version.get('tag')!r}")

    boards = ota.get("boards", {})
    if not isinstance(boards, dict) or set(boards) != EXPECTED_OTA_BOARDS:
        _fail(f"{path}: boards are {sorted(boards) if isinstance(boards, dict) else boards!r}, "
              f"expected {sorted(EXPECTED_OTA_BOARDS)}")
    for token, row in boards.items():
        if not isinstance(row, dict):
            _fail(f"{path}: {token} row is not an object")
        rel = row.get("path")
        if not isinstance(rel, str) or not rel or rel.startswith(("/", "../")) or ".." in Path(rel).parts:
            _fail(f"{path}: {token} path {rel!r} must stay inside the slot")
        f = site / rel
        if not f.is_file() or f.stat().st_size == 0:
            _fail(f"{path}: {token} references {rel!r} but {f} is missing or empty")
        md5, size = _md5_and_size(f)
        if row.get("bytes") != size:
            _fail(f"{path}: {token} advertises {row.get('bytes')!r} bytes, {rel} is {size}")
        if str(row.get("md5", "")).lower() != md5:
            _fail(f"{path}: {token} advertises md5 {row.get('md5')!r}, {rel} is {md5}")
        for field in ("versionCode", "partitionLayoutVersion"):
            value = row.get(field)
            if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
                _fail(f"{path}: {token}.{field} is {value!r}")


def check_site(site: Path, require_ota: bool = False, ota_channel: str = "stable"):
    manifest = _load(site, "manifest.json")
    version = _load(site, "version.json")
    if manifest.get("version") != version.get("tag"):
        _fail("manifest.json version disagrees with version.json tag")

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

    _check_ota(site, version, require_ota, ota_channel)

    alternative = version.get("alternativeRelease")
    if alternative:
        if alternative.get("path") == "previous/":
            previous = _load(site / "previous", "version.json")
            if previous.get("tag") != alternative.get("tag"):
                _fail("previous release tag disagrees with the advertised choice")
            if previous.get("alternativeRelease", {}).get("path") != "../":
                _fail("previous release has no return link")
            check_site(site / "previous")
        elif alternative.get("path") != "../":
            _fail("unsupported alternative release path")

    print(f"check_site: OK - {site}/manifest.json + version.json describe all 3 boards, "
          "every part file present")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--site", default="_site", type=Path, help="assembled site directory")
    ap.add_argument("--require-ota", action="store_true",
                    help="fail unless the slot carries a valid ota.json")
    ap.add_argument("--ota-channel", default="stable", choices=["stable", "dev"])
    args = ap.parse_args()
    if not args.site.is_dir():
        _fail(f"{args.site} is not a directory")
    check_site(args.site, args.require_ota, args.ota_channel)


if __name__ == "__main__":
    main()
