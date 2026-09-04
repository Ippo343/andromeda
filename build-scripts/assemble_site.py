#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Assembles the browser web-installer site (#162): reads each board's
flashparts/parts.json (from build-scripts/dump_flash_parts.py), validates
them against each other and against the partition table's NVS range, copies
the shared on-device UI assets so the installer looks like the real web UI,
and writes the ESP Web Tools manifest.json + a version.json for the page.

Not a PlatformIO extra_script - a plain CLI tool, same shape as
make_manifest.py. Called two ways, both exercising the exact same code path:

  - .github/workflows/pages.yml, pointed at downloaded release assets, on
    every stable release (the real deploy).
  - .github/workflows/test.yml's web-installer-assemble job, pointed straight
    at the hardware envs' own .pio/build/<env>/flashparts output, on every
    push/PR. No release, no tag, no fleet exposure - this is what keeps the
    installer pipeline part of "the full suite is green on every commit"
    instead of a manual dry-run someone has to remember to trigger.

--parts-dir must contain one subdirectory per board in BOARDS, each holding
parts.json plus every file it references (bootloader.bin/partitions.bin/
boot_app0.bin/firmware.bin/littlefs.bin) - already flat, already board-named.
Zipping/unzipping release assets into that shape is the caller's job, not
this script's.
"""

import argparse
import json
import re
import shutil
import sys
from pathlib import Path

# Must match include/board-variant.h's BOARD_VARIANT tokens and
# make_manifest.py's BOARDS list - the OTA manifest and this one describe the
# same three boards.
BOARDS = ["esp32_wroom", "esp32_s3_zero", "esp32_c3_zero"]

_KNOWN_CHIP_FAMILIES = {"ESP32", "ESP32-S3", "ESP32-C3"}

_CINZEL_FONT_FACE_RE = re.compile(
    r"@font-face\s*\{\s*font-family:\s*'Cinzel Decorative';\s*"
    r"src:\s*url\('/fonts/cinzel\.woff2'\)\s*format\('woff2'\);\s*"
    r"font-weight:\s*700;\s*font-style:\s*normal;\s*\}\s*"
)

_BOARD_LABELS = {
    "esp32_wroom": "ESP32-WROOM",
    "esp32_s3_zero": "ESP32-S3-Zero",
    "esp32_c3_zero": "ESP32-C3-Zero",
}

_REPO_ROOT = Path(__file__).resolve().parent.parent


def _load_parts(parts_dir: Path, env: str) -> dict:
    board_dir = parts_dir / env
    parts_json = board_dir / "parts.json"
    if not parts_json.is_file():
        sys.exit(f"assemble_site: missing {parts_json}")
    manifest = json.loads(parts_json.read_text(encoding="utf-8"))

    if manifest.get("board") != env:
        sys.exit(f"assemble_site: {parts_json} declares board={manifest.get('board')!r}, expected {env!r}")

    for part in manifest.get("parts", []):
        f = board_dir / part["file"]
        if not f.is_file() or f.stat().st_size == 0:
            sys.exit(f"assemble_site: {env}: missing or empty part file {f}")

    return manifest


def _validate(parts_dir: Path, manifests: dict):
    versions = {m["partitionLayoutVersion"] for m in manifests.values()}
    if len(versions) != 1:
        sys.exit(f"assemble_site: partitionLayoutVersion disagrees across boards: "
                  f"{ {env: m['partitionLayoutVersion'] for env, m in manifests.items()} }")

    families = {env: m["chipFamily"] for env, m in manifests.items()}
    if len(set(families.values())) != len(families):
        sys.exit(f"assemble_site: chipFamily values are not distinct: {families}")
    unknown = set(families.values()) - _KNOWN_CHIP_FAMILIES
    if unknown:
        sys.exit(f"assemble_site: unrecognised chipFamily value(s): {unknown} "
                  f"(expected one of {sorted(_KNOWN_CHIP_FAMILIES)})")

    for env, manifest in manifests.items():
        board_dir = parts_dir / env
        preserve = manifest.get("preserve", [])
        for part in manifest["parts"]:
            size = (board_dir / part["file"]).stat().st_size
            p_start, p_end = part["offset"], part["offset"] + size
            for guard in preserve:
                g_start, g_end = guard["offset"], guard["offset"] + guard["size"]
                if p_start < g_end and g_start < p_end:
                    sys.exit(
                        f"assemble_site: {env}: part '{part['name']}' "
                        f"[0x{p_start:x}, 0x{p_end:x}) overlaps preserved "
                        f"'{guard['name']}' [0x{g_start:x}, 0x{g_end:x}) - "
                        f"refusing to publish a manifest that could erase NVS"
                    )


def _copy_shared_ui_assets(site_dir: Path):
    """Reuse the real on-device UI assets (data/) so the installer matches
    its look - see data/index.html / data/css/common.css / data/js/utils.js.
    A one-way copy out of data/ costs the flashed filesystem nothing; nothing
    is ever added *into* data/ here (that tree is size-gated by
    check_fs_ceiling.py and route-diffed by test/test_native_suite/test_static_assets.cpp)."""
    data_dir = _REPO_ROOT / "data"
    (site_dir / "css").mkdir(parents=True, exist_ok=True)
    (site_dir / "js").mkdir(parents=True, exist_ok=True)

    common_css = (data_dir / "css" / "common.css").read_text(encoding="utf-8")
    # common.css's local @font-face loads data/fonts/cinzel.woff2 - a font
    # subsetted down to just the glyph "A" to minimize the flashed
    # filesystem's footprint (see check_fs_ceiling.py). The web installer has
    # no such constraint, so it isn't worth shipping/maintaining a second copy
    # of that subset here; strip the local @font-face and load the real,
    # full "Cinzel Decorative" face from Google Fonts instead (see
    # web-installer/index.html's <link>). .logo's `font-family: 'Cinzel
    # Decorative', serif` rule (further down in this same file) is untouched
    # and just resolves against whichever @font-face registers that name.
    if not _CINZEL_FONT_FACE_RE.search(common_css):
        sys.exit("assemble_site: data/css/common.css's Cinzel @font-face block no longer "
                  "matches what this strip expects - update the regex")
    common_css = _CINZEL_FONT_FACE_RE.sub("", common_css)
    (site_dir / "css" / "common.css").write_text(common_css, encoding="utf-8")

    utils_js = (data_dir / "js" / "utils.js").read_text(encoding="utf-8")
    if "randomGradient" not in utils_js:
        sys.exit("assemble_site: data/js/utils.js no longer defines randomGradient()")
    shutil.copyfile(data_dir / "js" / "utils.js", site_dir / "js" / "utils.js")


def _copy_installer_source(site_dir: Path):
    installer_dir = _REPO_ROOT / "web-installer"
    shutil.copyfile(installer_dir / "index.html", site_dir / "index.html")
    shutil.copyfile(installer_dir / "css" / "installer.css", site_dir / "css" / "installer.css")
    for js_file in (installer_dir / "js").glob("*.js"):
        shutil.copyfile(js_file, site_dir / "js" / js_file.name)


def _copy_board_parts(parts_dir: Path, site_dir: Path):
    for env in BOARDS:
        dest = site_dir / env
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(parts_dir / env, dest)


def _write_manifest(site_dir: Path, manifests: dict, tag: str):
    builds = []
    for env in BOARDS:
        manifest = manifests[env]
        parts = [
            {"path": f"{env}/{part['file']}", "offset": part["offset"]}
            for part in sorted(manifest["parts"], key=lambda p: p["offset"])
        ]
        builds.append({"chipFamily": manifest["chipFamily"], "parts": parts})

    # new_install_prompt_erase is load-bearing: true offers a full chip erase,
    # destroying the NVS that multi-part flashing (parts at their real
    # offsets, never the whole chip) exists to protect. improv: false skips
    # the Improv-Serial WiFi step the firmware doesn't implement - it would
    # otherwise hang instead of pointing the user at the captive portal.
    site_manifest = {
        "name": "Andromeda",
        "version": tag,
        "new_install_prompt_erase": False,
        "improv": False,
        "builds": builds,
    }
    (site_dir / "manifest.json").write_text(json.dumps(site_manifest, indent=2) + "\n", encoding="utf-8")


def _write_version_info(site_dir: Path, manifests: dict, tag: str, repo: str):
    boards = []
    for env in BOARDS:
        manifest = manifests[env]
        boards.append({
            "board": env,
            "chipFamily": manifest["chipFamily"],
            "label": _BOARD_LABELS[env],
            "parts": sorted(
                [{"name": p["name"], "offsetHex": p["offsetHex"]} for p in manifest["parts"]],
                key=lambda p: int(p["offsetHex"], 16),
            ),
        })
    release_url = f"https://github.com/{repo}/releases/tag/{tag}" if repo and tag else ""
    version_info = {"tag": tag, "releaseUrl": release_url, "boards": boards}
    (site_dir / "version.json").write_text(json.dumps(version_info, indent=2) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--parts-dir", required=True, type=Path,
                     help="directory with one <board>/ subdir per BOARDS entry")
    ap.add_argument("--out", required=True, type=Path, help="site output directory")
    ap.add_argument("--tag", default="local")
    ap.add_argument("--repo", default="")
    args = ap.parse_args()

    manifests = {env: _load_parts(args.parts_dir, env) for env in BOARDS}
    _validate(args.parts_dir, manifests)

    args.out.mkdir(parents=True, exist_ok=True)
    _copy_board_parts(args.parts_dir, args.out)
    _copy_shared_ui_assets(args.out)
    _copy_installer_source(args.out)
    _write_manifest(args.out, manifests, args.tag)
    _write_version_info(args.out, manifests, args.tag, args.repo)

    print(f"assemble_site: wrote {args.out} for tag={args.tag!r} ({len(BOARDS)} boards)")


if __name__ == "__main__":
    main()
