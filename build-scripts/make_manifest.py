#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Generates the manifest.json that ships as a GitHub Release asset for OTA (#63).

Not a PlatformIO extra_script - a plain CLI tool run by
.github/workflows/release.yml after the three hardware envs have been built
(firmware) and had their filesystem images built. For each board it records
the size + MD5 of firmware-<env>-<tag>.bin / littlefs-<env>-<tag>.bin and the
GitHub download URL those assets will have once uploaded to the release.

The schema must stay in lockstep with include/ota-manifest.h (OtaManifest::parse):

    {
      "channel": "stable" | "dev",
      "tag": "<git tag>",
      "boards": [
        { "board": "<env name>", "versionCode": <int>,
          "fw": { "url": "...", "bytes": <int>, "md5": "<32 hex>" },
          "fs": { "url": "...", "bytes": <int>, "md5": "<32 hex>" } },
        ...
      ]
    }
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

# The board token is the PlatformIO env name - it must equal BOARD_VARIANT in
# include/board-variant.h, which the device matches against.
BOARDS = ["esp32_wroom", "esp32_s3_zero", "esp32_c3_zero"]


def md5_and_size(path: Path):
    h = hashlib.md5()
    size = 0
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
            size += len(chunk)
    return h.hexdigest(), size


def asset(repo: str, tag: str, name: str, path: Path):
    if not path.is_file():
        sys.exit(f"make_manifest: expected build artifact not found: {path}")
    md5, size = md5_and_size(path)
    if size == 0:
        sys.exit(f"make_manifest: {path} is empty")
    return {
        "url": f"https://github.com/{repo}/releases/download/{tag}/{name}",
        "bytes": size,
        "md5": md5,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True, help="owner/repo, e.g. Ippo343/andromeda")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--channel", required=True, choices=["stable", "dev"])
    ap.add_argument("--version-code", required=True, type=int)
    ap.add_argument("--build-dir", default=".pio/build")
    ap.add_argument("--out", default="manifest.json")
    args = ap.parse_args()

    build_dir = Path(args.build_dir)
    boards = []
    for env in BOARDS:
        fw_bin = build_dir / env / "firmware.bin"
        fs_bin = build_dir / env / "littlefs.bin"
        boards.append({
            "board": env,
            "versionCode": args.version_code,
            "fw": asset(args.repo, args.tag, f"firmware-{env}-{args.tag}.bin", fw_bin),
            "fs": asset(args.repo, args.tag, f"littlefs-{env}-{args.tag}.bin", fs_bin),
        })

    manifest = {"channel": args.channel, "tag": args.tag, "boards": boards}
    Path(args.out).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.out} for {args.tag} ({args.channel}, code {args.version_code})")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
