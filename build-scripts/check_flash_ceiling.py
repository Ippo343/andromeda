"""Post-build size gate.

Fails the build when the linked firmware image exceeds a per-env ceiling,
expressed as a percentage of the app (OTA) partition. This is a tripwire for
*sudden* bloat - the kind that slipped in unnoticed when src/native-runtime.cpp
started being compiled into firmware and dragged ~230 KB of C++ iostreams /
locale / float-printf runtime along with it for ~50 commits.

Configure per env in platformio.ini:

    [env:esp32_wroom]
    custom_flash_ceiling_pct = 75

Envs without that option (native, native_runtime) are skipped. Raising the
ceiling is a deliberate one-line change with a reviewer looking at it - which
is the whole point.
"""

import os
import sys

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)


def _ceiling_pct(env):
    raw = env.GetProjectOption("custom_flash_ceiling_pct", None)
    if raw is None or str(raw).strip() == "":
        return None
    return float(raw)


def check_flash_ceiling(source, target, env):
    ceiling = _ceiling_pct(env)
    if ceiling is None:
        return

    bin_path = os.path.join(env.subst("$BUILD_DIR"), env.subst("${PROGNAME}.bin"))
    if not os.path.isfile(bin_path):
        return

    used = os.path.getsize(bin_path)
    partition = int(env.BoardConfig().get("upload.maximum_size", 0))
    if partition <= 0:
        print("size gate: no known app partition size, skipping")
        return

    pct = used / partition * 100.0
    summary = "%d / %d bytes = %.1f%% of app partition (ceiling %.1f%%)" % (
        used, partition, pct, ceiling)

    if pct > ceiling:
        print("")
        print("*** SIZE GATE FAILED: " + summary)
        print("*** Find what grew (`pio run -e <env> -t size`, or diff the map file),")
        print("*** or raise custom_flash_ceiling_pct in platformio.ini on purpose.")
        print("")
        sys.exit(1)

    print("size gate OK: " + summary)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_flash_ceiling)  # noqa: F821
