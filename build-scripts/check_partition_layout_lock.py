"""Build gate: the partition table and PARTITION_LAYOUT_VERSION move together.

Shipping OTA freezes partitions/andromeda_4mb.csv for the fleet - it lives at
flash offset 0x8000, outside every OTA-writable partition, so a device can only
get a new table via a USB reflash (#63). PARTITION_LAYOUT_VERSION
(include/board-variant.h) is the deliberate "yes, I know that" switch, and
test/test_partition_layout pins the CSV's values against it.

The gap this closes: you could still edit the CSV *and* that test's expected
table together and forget to bump the version, or bump the version without
touching the CSV. This script makes either one a hardware-build failure by
pinning a digest of (canonical CSV rows + version) into board-variant.h as
PARTITION_LAYOUT_DIGEST. Any change to the CSV layout or the version number
that isn't accompanied by a refreshed digest fails the build.

Deliberate change workflow:
    1. edit partitions/andromeda_4mb.csv and/or bump PARTITION_LAYOUT_VERSION
    2. python build-scripts/check_partition_layout_lock.py --update
    3. update test/test_partition_layout's expected table + #error guard
    4. USB-reflash every deployed unit

Runs as a pre: extra_script on the hardware envs (no-op for native, which has
no board_build.partitions), and standalone with --update to rewrite the digest.
"""

import hashlib
import re
import sys
from pathlib import Path

CSV_REL = "partitions/andromeda_4mb.csv"
HEADER_REL = "include/board-variant.h"
VERSION_RE = re.compile(r"^#define\s+PARTITION_LAYOUT_VERSION\s+(\d+)\s*$", re.M)
DIGEST_RE = re.compile(r'^#define\s+PARTITION_LAYOUT_DIGEST\s+"([0-9a-f]{64})"\s*$', re.M)


def canonical_digest(csv_text: str, version: int) -> str:
    """Stable hash of the semantic layout - comments / spacing don't count,
    but any partition's name/type/subtype/offset/size, their order, or the
    version number does."""
    rows = []
    for line in csv_text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            sys.exit(f"check_partition_layout_lock: malformed CSV row: {line!r}")
        name, ptype, subtype = cols[0], cols[1], cols[2]
        offset = int(cols[3], 0)
        size = int(cols[4], 0)
        rows.append(f"{name},{ptype},{subtype},0x{offset:x},0x{size:x}")
    material = "\n".join(rows) + f"\nversion={version}\n"
    return hashlib.sha256(material.encode()).hexdigest()


def _read(project_dir: Path):
    csv_path = project_dir / CSV_REL
    header_path = project_dir / HEADER_REL
    csv_text = csv_path.read_text(encoding="utf-8")
    header_text = header_path.read_text(encoding="utf-8")

    vm = VERSION_RE.search(header_text)
    if not vm:
        sys.exit(f"check_partition_layout_lock: no PARTITION_LAYOUT_VERSION in {HEADER_REL}")
    version = int(vm.group(1))
    dm = DIGEST_RE.search(header_text)
    recorded = dm.group(1) if dm else None
    return header_path, header_text, version, recorded, canonical_digest(csv_text, version)


def _check(project_dir: Path):
    _, _, version, recorded, actual = _read(project_dir)
    if recorded is None:
        sys.exit(
            f"check_partition_layout_lock: {HEADER_REL} has no PARTITION_LAYOUT_DIGEST.\n"
            f"  Add:  #define PARTITION_LAYOUT_DIGEST \"{actual}\"\n"
            f"  or run: python build-scripts/check_partition_layout_lock.py --update"
        )
    if recorded != actual:
        sys.exit(
            "\n*** PARTITION LAYOUT LOCK FAILED ***\n"
            f"  {CSV_REL} (canonical layout + PARTITION_LAYOUT_VERSION={version})\n"
            f"  hashes to   {actual}\n"
            f"  but {HEADER_REL} pins  {recorded}\n\n"
            "  The partition table and PARTITION_LAYOUT_VERSION must change together.\n"
            "  If this change is deliberate: bump PARTITION_LAYOUT_VERSION, run\n"
            "  `python build-scripts/check_partition_layout_lock.py --update`, update\n"
            "  test/test_partition_layout, and USB-reflash every deployed unit -\n"
            "  a new table cannot be delivered over OTA.\n"
        )
    print(f"partition layout lock OK: v{version} {actual[:12]}...")


def _update(project_dir: Path):
    header_path, header_text, version, recorded, actual = _read(project_dir)
    if recorded == actual:
        print(f"partition layout digest already current: {actual}")
        return
    if DIGEST_RE.search(header_text):
        new_text = DIGEST_RE.sub(f'#define PARTITION_LAYOUT_DIGEST "{actual}"', header_text)
    else:
        new_text = VERSION_RE.sub(
            lambda m: m.group(0) + f'\n#define PARTITION_LAYOUT_DIGEST "{actual}"', header_text
        )
    header_path.write_text(new_text, encoding="utf-8")
    print(f"partition layout digest updated for v{version}: {actual}")


# --- standalone (--update) vs PlatformIO pre: extra_script -------------------

try:
    Import("env")  # noqa: F821  (injected by PlatformIO/SCons)
    _AS_EXTRA_SCRIPT = True
except NameError:
    _AS_EXTRA_SCRIPT = False

if _AS_EXTRA_SCRIPT:
    if env.GetProjectOption("board_build.partitions", None):  # noqa: F821
        _check(Path(env.subst("$PROJECT_DIR")))  # noqa: F821
elif __name__ == "__main__":
    repo = Path(__file__).resolve().parent.parent
    if "--update" in sys.argv[1:]:
        _update(repo)
    else:
        _check(repo)
