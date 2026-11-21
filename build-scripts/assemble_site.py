#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Assembles the browser web-installer site (#162): reads each board's
flashparts/parts.json (from build-scripts/dump_flash_parts.py), validates
them against each other and against the partition table's NVS range, copies
the shared on-device UI assets so the installer looks like the real web UI,
and writes the ESP Web Tools manifest.json + a version.json for the page.

With --ota-manifest it also writes ota.json, the update index the on-device
SPA reads (channel, version code, layout, size and md5 per board). That file
is derived from the release's own manifest.json but every number is re-derived
from the bytes being served, so the site can never advertise an image it does
not have.

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
boot_app0.bin/firmware.bin) - already flat, already board-named.
Zipping/unzipping release assets into that shape is the caller's job, not
this script's.
"""

import argparse
import hashlib
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

# The bootloader's flash offset is the one address in the triplet that differs
# per chip: ESP32 keeps it after the ROM's 0x1000-byte header, S3 and C3 at 0.
# Everything else (partition table, otadata) lines up across all three.
_BOOTLOADER_OFFSETS = {"ESP32": 0x1000, "ESP32-S3": 0x0, "ESP32-C3": 0x0}

_CINZEL_FONT_FACE_RE = re.compile(
    r"@font-face\s*\{\s*font-family:\s*'Cinzel Decorative';\s*"
    r"src:\s*url\('(?:\.\./)?fonts/cinzel\.woff2'\)\s*format\('woff2'\);\s*"
    r"font-weight:\s*700;\s*font-style:\s*normal;\s*\}\s*"
)

_BOARD_LABELS = {
    "esp32_wroom": "ESP32-WROOM",
    "esp32_s3_zero": "ESP32-S3-Zero",
    "esp32_c3_zero": "ESP32-C3-Zero",
}

# The OTA manifest's row token per board (`include/board-variant.h`'s
# OTA_BOARD_VARIANT). Duplicated from build-scripts/board_meta.py on purpose:
# only assemble_site.py and check_site.py are synced into the release repo
# (tools/release-local.ps1), and importing board_meta there would need a third
# file on that sync list. test_web_installer.cpp pins the tokens against
# board-variant.h.
_OTA_BOARD_TOKENS = {env: f"{env}_v2" for env in BOARDS}

# ESP-IDF partition-table binary: 32-byte little-endian rows, magic 0x50AA,
# terminated by the 0xEBEB md5 entry. Only the two rows the installer needs to
# agree with are decoded.
_PARTITION_MAGIC = 0x50AA
_PARTITION_END_MAGIC = 0xEBEB
_TYPE_APP = 0x00
_TYPE_DATA = 0x01
_SUBTYPE_OTA_0 = 0x10
_SUBTYPE_OTADATA = 0x00

_REPO_ROOT = Path(__file__).resolve().parent.parent


def _partition_table(path: Path):
    data = path.read_bytes()
    rows = []
    for start in range(0, len(data) - 31, 32):
        magic = int.from_bytes(data[start:start + 2], "little")
        if magic == _PARTITION_END_MAGIC:
            break
        if magic != _PARTITION_MAGIC:
            sys.exit(f"assemble_site: {path}: bad partition-table magic at 0x{start:x}")
        rows.append({
            "type": data[start + 2],
            "subtype": data[start + 3],
            "offset": int.from_bytes(data[start + 4:start + 8], "little"),
            "size": int.from_bytes(data[start + 8:start + 12], "little"),
            "label": data[start + 12:start + 28].split(b"\0", 1)[0].decode("ascii", "replace"),
        })
    if not rows:
        sys.exit(f"assemble_site: {path}: empty partition table")
    return rows


def _check_offsets_against_table(board_dir: Path, parts, sizes, chip_family: str):
    """parts.json and partitions.bin must describe the same flash layout.

    parts.json decides where the installer writes; partitions.bin is what the
    bootloader will actually read. They come from one build, so a mismatch means
    a stale or hand-assembled release - and firmware written to 0x20000 under a
    table whose app slot starts at 0x10000 yields a board that cannot boot, with
    no error anywhere.
    """
    table = _partition_table(board_dir / "partitions.bin")
    app = next((r for r in table if r["type"] == _TYPE_APP and r["subtype"] == _SUBTYPE_OTA_0),
               None)
    otadata = next((r for r in table if r["type"] == _TYPE_DATA and r["subtype"] == _SUBTYPE_OTADATA),
                   None)
    if app is None:
        sys.exit(f"assemble_site: {board_dir}: partition table has no ota_0 app slot")
    if otadata is None:
        sys.exit(f"assemble_site: {board_dir}: partition table has no otadata row")

    by_name = {p["name"]: p for p in parts}
    firmware = by_name["firmware"]
    if firmware["offset"] != app["offset"]:
        sys.exit(f"assemble_site: {board_dir}: firmware is written at "
                 f"0x{firmware['offset']:x} but the partition table puts ota_0 at "
                 f"0x{app['offset']:x}")
    if sizes["firmware"] > app["size"]:
        sys.exit(f"assemble_site: {board_dir}: firmware ({sizes['firmware']} bytes) does "
                 f"not fit the 0x{app['size']:x}-byte ota_0 slot")

    boot_app0 = by_name.get("boot_app0")
    if boot_app0 is not None and boot_app0["offset"] != otadata["offset"]:
        sys.exit(f"assemble_site: {board_dir}: boot_app0 is written at "
                 f"0x{boot_app0['offset']:x} but the partition table puts otadata at "
                 f"0x{otadata['offset']:x}")

    bootloader = by_name.get("bootloader")
    expected_bootloader = _BOOTLOADER_OFFSETS.get(chip_family)
    if bootloader is not None and expected_bootloader is not None and \
            bootloader["offset"] != expected_bootloader:
        sys.exit(f"assemble_site: {board_dir}: bootloader is written at "
                 f"0x{bootloader['offset']:x} but a {chip_family} board boots it from "
                 f"0x{expected_bootloader:x}")


def _load_parts(parts_dir: Path, env: str) -> dict:
    board_dir = parts_dir / env
    parts_json = board_dir / "parts.json"
    if not parts_json.is_file():
        sys.exit(f"assemble_site: missing {parts_json}")
    manifest = json.loads(parts_json.read_text(encoding="utf-8"))

    if manifest.get("board") != env:
        sys.exit(f"assemble_site: {parts_json} declares board={manifest.get('board')!r}, expected {env!r}")

    sizes = {}
    for part in manifest.get("parts", []):
        f = board_dir / part["file"]
        if not f.is_file() or f.stat().st_size == 0:
            sys.exit(f"assemble_site: {env}: missing or empty part file {f}")
        size = f.stat().st_size
        name = part["name"]
        sizes[name] = size
        if ((name == "boot_app0" and size != 0x2000)
                or (name == "partitions" and size != 0xC00)
                or (name in ("firmware", "bootloader") and size < 0x1000)):
            sys.exit(f"assemble_site: {env}: invalid {name} size ({size} bytes)")
        if name == "firmware":
            with f.open("rb") as image:
                if image.read(1) != b"\xe9":
                    sys.exit(f"assemble_site: {env}: invalid firmware magic")

    # An installer manifest without the application image still flashes boot
    # parts happily, and a bare board ends up with nothing to boot. It is the
    # one part every build produces, so its absence means the parts dir was
    # assembled by hand or half-copied.
    if "firmware" not in sizes:
        sys.exit(f"assemble_site: {env}: {parts_json} lists no firmware part")

    _check_offsets_against_table(board_dir, manifest.get("parts", []), sizes,
                                 str(manifest.get("chipFamily", "")))
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
    The installer copies assets out of data/. build_ui.cjs budgets the firmware
    document independently."""
    data_dir = _REPO_ROOT / "data"
    (site_dir / "css").mkdir(parents=True, exist_ok=True)
    (site_dir / "js").mkdir(parents=True, exist_ok=True)

    common_css = (data_dir / "css" / "common.css").read_text(encoding="utf-8")
    # common.css's local @font-face loads data/fonts/cinzel.woff2 - a font
    # subsetted down to just the glyph "A" to reduce embedded document size.
    # The web installer has
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


def _write_version_info(site_dir: Path, manifests: dict, tag: str, repo: str, alternative=None):
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
    if alternative:
        version_info["alternativeRelease"] = alternative
    (site_dir / "version.json").write_text(json.dumps(version_info, indent=2) + "\n", encoding="utf-8")


def _md5_and_size(path: Path):
    """(md5 hex, byte size) - one streaming pass, same shape as verify_release.py."""
    digest = hashlib.md5()
    size = 0
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def _positive_int(value, what: str):
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        sys.exit(f"assemble_site: {what} is {value!r}, expected a positive integer")
    return value


def _write_ota_manifest(site_dir: Path, ota_manifest: Path, tag: str, channel: str):
    """Write the machine-readable update index the on-device SPA reads.

    Derived from the release's own manifest.json, but every number is
    re-derived from the bytes that will actually be served: a site that
    advertises a size or md5 its file does not have is worse than no site at
    all, because the device would flash it and fail at Update.end().
    """
    try:
        manifest = json.loads(ota_manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        sys.exit(f"assemble_site: {ota_manifest}: {e}")

    if manifest.get("tag") != tag:
        sys.exit(f"assemble_site: {ota_manifest} describes {manifest.get('tag')!r}, not {tag!r}")
    if manifest.get("channel") != channel:
        sys.exit(f"assemble_site: {ota_manifest} is the {manifest.get('channel')!r} channel, "
                 f"asked to publish it as {channel!r}")

    rows = {}
    for row in manifest.get("boards", []):
        if isinstance(row, dict) and row.get("board"):
            rows[row["board"]] = row

    boards = {}
    for env in BOARDS:
        token = _OTA_BOARD_TOKENS[env]
        row = rows.get(token)
        if row is None:
            sys.exit(f"assemble_site: {ota_manifest} has no {token!r} row")

        rel = f"{env}/firmware.bin"
        path = site_dir / rel
        if not path.is_file():
            sys.exit(f"assemble_site: {path} is missing - did the parts copy run?")

        fw = row.get("fw")
        if not isinstance(fw, dict):
            sys.exit(f"assemble_site: {token}: no fw object")
        want_md5 = str(fw.get("md5", "")).lower()
        want_bytes = _positive_int(fw.get("bytes"), f"{token}.fw.bytes")
        got_md5, got_bytes = _md5_and_size(path)
        if got_bytes != want_bytes:
            sys.exit(f"assemble_site: {token}: firmware is {got_bytes} bytes, "
                     f"the manifest says {want_bytes}")
        if got_md5 != want_md5:
            sys.exit(f"assemble_site: {token}: firmware md5 {got_md5} does not match "
                     f"the manifest's {want_md5}")

        boards[token] = {
            "versionCode": _positive_int(row.get("versionCode"), f"{token}.versionCode"),
            # Fail closed exactly like the device does: a row that does not
            # declare its partition layout cannot be published for OTA.
            "partitionLayoutVersion": _positive_int(row.get("partitionLayoutVersion"),
                                                    f"{token}.partitionLayoutVersion"),
            "minSourceVersionCode": int(row.get("minSourceVersionCode") or 0),
            "bytes": got_bytes,
            "md5": got_md5,
            "path": rel,
        }

    ota = {"schema": 1, "channel": channel, "tag": tag, "boards": boards}
    (site_dir / "ota.json").write_text(json.dumps(ota, indent=2) + "\n", encoding="utf-8")


def assemble(parts_dir: Path, out: Path, tag: str, repo: str, alternative=None,
             ota_manifest: Path = None, ota_channel: str = None):
    manifests = {env: _load_parts(parts_dir, env) for env in BOARDS}
    _validate(parts_dir, manifests)
    out.mkdir(parents=True, exist_ok=True)
    _copy_board_parts(parts_dir, out)
    _copy_shared_ui_assets(out)
    _copy_installer_source(out)
    _write_manifest(out, manifests, tag)
    _write_version_info(out, manifests, tag, repo, alternative)
    if ota_manifest is not None:
        _write_ota_manifest(out, ota_manifest, tag, ota_channel)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--parts-dir", required=True, type=Path,
                     help="directory with one <board>/ subdir per BOARDS entry")
    ap.add_argument("--out", required=True, type=Path, help="site output directory")
    ap.add_argument("--tag", default="local")
    ap.add_argument("--repo", default="")
    ap.add_argument("--previous-parts-dir", type=Path)
    ap.add_argument("--previous-tag")
    ap.add_argument("--ota-manifest", type=Path,
                    help="the release's OTA manifest.json; emits ota.json for the SPA")
    ap.add_argument("--ota-channel", choices=["stable", "dev"],
                    help="channel ota.json advertises (required with --ota-manifest)")
    args = ap.parse_args()

    if bool(args.previous_parts_dir) != bool(args.previous_tag):
        ap.error("--previous-parts-dir and --previous-tag must be supplied together")
    if bool(args.ota_manifest) != bool(args.ota_channel):
        ap.error("--ota-manifest and --ota-channel must be supplied together")
    alternative = None
    if args.previous_tag:
        if args.previous_tag == args.tag:
            ap.error("previous release must differ from the current release")
        assemble(args.previous_parts_dir, args.out / "previous", args.previous_tag, args.repo,
                 {"tag": args.tag, "path": "../", "label": "Return to latest stable"})
        alternative = {"tag": args.previous_tag, "path": "previous/",
                       "label": "Having trouble? Try the previous stable release"}
    assemble(args.parts_dir, args.out, args.tag, args.repo, alternative,
             args.ota_manifest, args.ota_channel)

    print(f"assemble_site: wrote {args.out} for tag={args.tag!r} ({len(BOARDS)} boards)")


if __name__ == "__main__":
    main()
