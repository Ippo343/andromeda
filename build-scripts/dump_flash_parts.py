"""Exports the boot-time flash parts (bootloader/partitions/boot_app0) plus an
offset manifest for the browser web installer (#162), so it can flash a bare
ESP32 over Web Serial the same way `pio run -t upload` does over USB.

Registers a `flashparts` PlatformIO target - `pio run -e <env> -t flashparts`
- rather than an automatic pre:/post: action, since it's only needed by
.github/workflows/release.yml and .github/workflows/test.yml's
hardware-build job, not by a normal `pio run`. No-op for native envs
(guarded the same way check_partition_layout_lock.py is: only hardware envs
set board_build.partitions).

Every offset is read out of PlatformIO's own resolved build environment -
FLASH_EXTRA_IMAGES for the bootloader/partition-table/boot_app0 triplet,
$ESP32_APP_OFFSET for the app slot, the resolved $PARTITIONS_TABLE_CSV for the
filesystem slot and the nvs row - never a literal. The bootloader address in
particular differs by chip (one address on ESP32, a different one on S3/C3)
and would be wrong half the time if hard-coded.

Writes $BUILD_DIR/flashparts/:
  bootloader.bin, partitions.bin, boot_app0.bin  (copied out - "bundled" parts)
  parts.json                                     (the offset manifest, below)

firmware.bin and littlefs.bin are NOT copied here - they already exist as
their own release assets (dist/firmware-<env>-<tag>.bin etc in release.yml);
parts.json just records where they belong ("bundled": false) and
build-scripts/assemble_site.py places them into the site itself.

parts.json shape:
    {
      "board": "esp32_s3_zero", "chip": "esp32s3", "chipFamily": "ESP32-S3",
      "flashSize": "4MB", "bootloaderFlashSizePatched": "4MB",
      "partitionLayoutVersion": 1,
      "preserve": [ {"name": "nvs", "offset": 36864, "size": 20480} ],
      "parts": [
        {"name": "bootloader", "file": "bootloader.bin", "offset": 0,
         "offsetHex": "0x0", "bundled": true},
        ...
      ]
    }

`preserve` is the executable form of "a re-install never touches NVS, so WiFi
credentials and settings survive it" - assemble_site.py refuses to publish a
manifest where any part's byte range overlaps it.

Bootloader flash-size header patch: `pio run -t upload` passes
`--flash_size <N>` to esptool.py, which rewrites byte 3 of the bootloader
image to match; esptool-js instead flashes with flashSize: "keep", so the
framework's prebuilt esp32-s3-devkitc-1 bootloader would still declare its
stock 8 MB (board_upload.flash_size = 4MB in platformio.ini overrides this
everywhere else). Patch the high nibble of that byte to the size code for this
board's *actual* upload.flash_size before copying it out.
"""

import csv
import json
import os
import shutil
import sys

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

# esptool's bootloader header flash-size nibble codes (byte 3, high nibble).
# See esptool's image_header layout / `esptool.py image_info`.
_FLASH_SIZE_CODES = {"1MB": 0x0, "2MB": 0x1, "4MB": 0x2, "8MB": 0x3, "16MB": 0x4}

_CHIP_FAMILY = {"esp32": "ESP32", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3"}


def _parse_size(raw):
    """Partition CSV sizes are hex (0xA0000), decimal, or K/M-suffixed."""
    text = str(raw).strip()
    if not text:
        return 0
    multiplier = 1
    if text[-1] in "kK":
        multiplier, text = 1024, text[:-1]
    elif text[-1] in "mM":
        multiplier, text = 1024 * 1024, text[:-1]
    return int(text, 0) * multiplier


def _partition_rows(csv_path):
    """name -> (offset, size) for every data row in the resolved table."""
    rows = {}
    with open(csv_path, "r", encoding="utf-8") as f:
        for row in csv.reader(f):
            if not row or row[0].strip().startswith("#") or len(row) < 5:
                continue
            name = row[0].strip()
            rows[name] = (_parse_size(row[3]), _parse_size(row[4]))
    return rows


def _patch_bootloader_flash_size(path, flash_size):
    code = _FLASH_SIZE_CODES.get(flash_size)
    if code is None:
        sys.exit("dump_flash_parts: unknown board upload.flash_size %r" % flash_size)
    with open(path, "r+b") as f:
        f.seek(3)
        byte3 = f.read(1)[0]
        f.seek(3)
        f.write(bytes([(code << 4) | (byte3 & 0x0F)]))


def dump_flash_parts(*_args, **_kwargs):
    board = env.BoardConfig()  # noqa: F821
    mcu = board.get("build.mcu")
    chip_family = _CHIP_FAMILY.get(mcu)
    if chip_family is None:
        sys.exit("dump_flash_parts: no chipFamily mapping for build.mcu=%r" % mcu)

    build_dir = env.subst("$BUILD_DIR")  # noqa: F821
    out_dir = os.path.join(build_dir, "flashparts")
    os.makedirs(out_dir, exist_ok=True)

    firmware_bin = os.path.join(build_dir, "firmware.bin")
    littlefs_bin = os.path.join(build_dir, "littlefs.bin")
    for required in (firmware_bin, littlefs_bin):
        if not os.path.isfile(required):
            sys.exit(
                "dump_flash_parts: expected build artifact not found: %s\n"
                "  (run `-t buildfs` before `-t flashparts`)" % required
            )

    csv_path = env.subst("$PARTITIONS_TABLE_CSV")  # noqa: F821
    if not csv_path or not os.path.isfile(csv_path):
        sys.exit("dump_flash_parts: no resolved partition table (PARTITIONS_TABLE_CSV)")
    table = _partition_rows(csv_path)
    if "nvs" not in table:
        sys.exit("dump_flash_parts: partition table has no 'nvs' row: %s" % csv_path)
    if "spiffs" not in table:
        sys.exit("dump_flash_parts: partition table has no 'spiffs' (LittleFS) row: %s" % csv_path)
    nvs_offset, nvs_size = table["nvs"]
    fs_offset, _fs_size = table["spiffs"]

    flash_size = str(board.get("upload.flash_size", "4MB"))

    parts = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):  # noqa: F821
        offset_int = _parse_size(offset)
        src = env.subst(path)  # noqa: F821
        name = os.path.splitext(os.path.basename(src))[0]
        dest_name = os.path.basename(src)
        shutil.copyfile(src, os.path.join(out_dir, dest_name))
        parts.append({
            "name": name, "file": dest_name,
            "offset": offset_int, "offsetHex": hex(offset_int),
            "bundled": True,
        })

    bootloader_out = os.path.join(out_dir, "bootloader.bin")
    if os.path.isfile(bootloader_out):
        _patch_bootloader_flash_size(bootloader_out, flash_size)

    app_offset = _parse_size(env.subst("$ESP32_APP_OFFSET"))  # noqa: F821
    parts.append({
        "name": "firmware", "file": "firmware.bin",
        "offset": app_offset, "offsetHex": hex(app_offset),
        "bundled": False,
    })
    parts.append({
        "name": "littlefs", "file": "littlefs.bin",
        "offset": fs_offset, "offsetHex": hex(fs_offset),
        "bundled": False,
    })
    parts.sort(key=lambda p: p["offset"])

    manifest = {
        "board": env["PIOENV"],  # noqa: F821
        "chip": mcu,
        "chipFamily": chip_family,
        "flashSize": flash_size,
        "bootloaderFlashSizePatched": flash_size,
        "partitionLayoutVersion": _partition_layout_version(),
        "preserve": [{"name": "nvs", "offset": nvs_offset, "size": nvs_size}],
        "parts": parts,
    }
    with open(os.path.join(out_dir, "parts.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print("dump_flash_parts: wrote %s (%d parts, chipFamily=%s)" %
          (out_dir, len(parts), chip_family))


def _partition_layout_version():
    """Read PARTITION_LAYOUT_VERSION out of include/board-variant.h - kept in
    lockstep with the CSV by check_partition_layout_lock.py, so the manifest
    can trust it rather than re-deriving anything."""
    header = os.path.join(env.subst("$PROJECT_DIR"), "include", "board-variant.h")  # noqa: F821
    with open(header, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("#define PARTITION_LAYOUT_VERSION"):
                return int(line.split()[-1])
    sys.exit("dump_flash_parts: PARTITION_LAYOUT_VERSION not found in %s" % header)


# Hardware envs only - same guard check_partition_layout_lock.py uses.
if env.GetProjectOption("board_build.partitions", None):  # noqa: F821
    env.AddTarget(  # noqa: F821
        name="flashparts",
        dependencies="$BUILD_DIR/${PROGNAME}.bin",
        actions=[env.VerboseAction(dump_flash_parts, "Exporting web-installer flash parts")],  # noqa: F821
        title="Flash Parts",
        description="Export offset-tagged boot parts + manifest for the web installer (#162)",
    )
