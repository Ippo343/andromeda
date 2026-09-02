"""Post-buildfs headroom gate for the data/ directory.

The firmware halves have had a size gate since check_flash_ceiling.py, but the
filesystem side had none - and until this landed, `-t buildfs` only ever ran in
.github/workflows/release.yml. So a data/ change that overflowed the spiffs
partition passed every PR check and first failed *on the release tag*, after
both firmware halves were already built and staged: the release half-produced
and the fix needing a new tag.

The hard failure is already handled: mklittlefs itself errors when the content
doesn't fit, and CI now runs buildfs on every PR (.github/workflows/test.yml),
so that error surfaces on the PR where it is cheap. This script is the *early
warning* ahead of it - the same role check_flash_ceiling.py plays for firmware.

It deliberately measures the total size of data/, NOT littlefs.bin. mklittlefs
emits a full-partition image, so littlefs.bin is *always* exactly the partition
size (655360 bytes here) whether data/ holds 5 KB or 500 KB - a gate comparing
that against the partition can never fire. Summing the source files is the only
measurement that tracks what actually grows.

The raw sum understates real usage: LittleFS rounds every file up to a block
and spends blocks on metadata. The default ceiling is set well under 100% to
absorb that, so this trips before mklittlefs does rather than after.

The capacity comes from the partition table PlatformIO resolved for this env,
not a hard-coded byte count: partitions/andromeda_4mb.csv is frozen for the
fleet (it sits outside every OTA-writable partition - see
include/board-variant.h's PARTITION_LAYOUT_VERSION), but an env that changed
board_build.partitions must be measured against *its* table.
"""

import csv
import os
import sys

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

# The data partition's subtype in the ESP-IDF partition table. Named "spiffs"
# for historical reasons even though the image is LittleFS - that is the
# subtype the bootloader and esptool understand, and what the CSV declares.
_DATA_SUBTYPE = "spiffs"


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


def _data_partition_size(env):
    """Size in bytes of the spiffs partition in this env's resolved table."""
    csv_path = env.subst("$PARTITIONS_TABLE_CSV")
    if not csv_path or not os.path.isfile(csv_path):
        return 0, csv_path

    with open(csv_path, "r", encoding="utf-8") as f:
        for row in csv.reader(f):
            # Skip comments and short/blank lines. Columns are:
            # name, type, subtype, offset, size, flags
            if not row or row[0].strip().startswith("#"):
                continue
            if len(row) < 5:
                continue
            if row[2].strip().lower() == _DATA_SUBTYPE:
                return _parse_size(row[4]), csv_path

    return 0, csv_path


def _data_dir_size(env):
    """Total bytes of the files that go into the image, and how many."""
    data_dir = env.subst("$PROJECT_DATA_DIR")
    if not data_dir or not os.path.isdir(data_dir):
        return 0, 0

    total = 0
    count = 0
    for root, _dirs, files in os.walk(data_dir):
        for name in files:
            total += os.path.getsize(os.path.join(root, name))
            count += 1
    return total, count


def check_fs_ceiling(source, target, env):
    ceiling = float(env.GetProjectOption("custom_fs_ceiling_pct", 75))

    used, file_count = _data_dir_size(env)
    if file_count == 0:
        return

    capacity, csv_path = _data_partition_size(env)
    if capacity <= 0:
        print("")
        print("*** FS SIZE GATE SKIPPED: no '%s' partition found" % _DATA_SUBTYPE)
        print("*** partition table: %s" % (csv_path or "<unresolved>"))
        print("*** data/ was NOT checked against a ceiling - if this env is")
        print("*** supposed to have a data partition, that is a real problem.")
        print("")
        return

    pct = used / capacity * 100.0
    summary = "%d bytes across %d files = %.1f%% of the %d-byte %s partition (ceiling %.1f%%)" % (
        used, file_count, pct, capacity, _DATA_SUBTYPE, ceiling)

    if pct > ceiling:
        print("")
        print("*** FS SIZE GATE FAILED: " + summary)
        print("*** data/ is close enough to filling the partition that LittleFS's")
        print("*** per-file block rounding and metadata could push it over - at which")
        print("*** point mklittlefs fails the build outright.")
        print("*** Shrink data/ (fonts and images are the usual culprits), or raise")
        print("*** custom_fs_ceiling_pct in platformio.ini on purpose. Resizing the")
        print("*** partition is a PARTITION_LAYOUT_VERSION bump and breaks OTA for")
        print("*** every device already in the field.")
        print("")
        sys.exit(1)

    print("fs size gate OK: " + summary)


env.AddPostAction("$BUILD_DIR/littlefs.bin", check_fs_ceiling)  # noqa: F821
