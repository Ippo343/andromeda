#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Issue #107: guard AsyncServer::_accept() in the vendored (stale) AsyncTCP
against a NULL tcp_pcb / non-OK err.

lwIP can invoke the accept callback with a NULL pcb (or err != ERR_OK) when a
connection is accepted but its PCB is freed before the callback runs - out of
TCP PCBs, or an RST between the SYN and the callback. The stale AsyncTCP that
lacamera/ESPAsyncWebServer#3.1.0 vendors (AsyncTCP-esphome) then does
`new AsyncClient(pcb)` and dereferences NULL -> "Load access fault" panic,
which two SoftAP clients opening the settings pages at once reproduce reliably.

Maintained AsyncTCP forks carry this guard; our pinned copy does not. Rather
than swap the whole library, this pre-build step inserts the guard into the
vendored source in-place. It is idempotent (keyed on ANDROMEDA_107_ACCEPT_GUARD)
and a no-op for envs that don't pull AsyncTCP (the native test/sim envs).
"""

Import("env")
import glob
import os
import re

SENTINEL = "ANDROMEDA_107_ACCEPT_GUARD"

GUARD = """    // {sentinel} - inserted by build-scripts/patch_asynctcp_accept.py (issue #107).
    // lwIP may call this with pcb == NULL (or err != ERR_OK) when a SYN is
    // accepted but the PCB is gone before the callback runs. The unpatched body
    // below would `new AsyncClient(pcb)` and dereference NULL -> panic.
    if (pcb == NULL || err != ERR_OK) {{
        if (pcb != NULL) {{
            tcp_abort(pcb);
        }}
        return ERR_OK;
    }}
""".format(sentinel=SENTINEL)

# Matches the function's opening line regardless of minor whitespace / arg-name
# differences between AsyncTCP fork revisions.
OPEN_RE = re.compile(
    r"(int8_t\s+AsyncServer::_accept\s*\(\s*tcp_pcb\s*\*\s*\w+\s*,\s*int8_t\s+\w+\s*\)\s*\{)"
)


def patch_file(path):
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        src = f.read()

    if SENTINEL in src:
        print("  already guarded: {}".format(path))
        return False

    new_src, n = OPEN_RE.subn(lambda m: m.group(1) + "\n" + GUARD, src, count=1)
    if n == 0:
        print("  WARNING: AsyncServer::_accept not found in {} - not patched".format(path))
        return False

    with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(new_src)
    print("  guarded AsyncServer::_accept in {}".format(path))
    return True


def main():
    libdeps_dir = env.get("PROJECT_LIBDEPS_DIR")
    pioenv = env.get("PIOENV")
    if not libdeps_dir or not pioenv:
        return

    env_libdeps = os.path.join(libdeps_dir, pioenv)
    candidates = glob.glob(os.path.join(env_libdeps, "AsyncTCP*", "src", "AsyncTCP.cpp"))
    if not candidates:
        # Native / simulator envs, or libs not resolved yet - nothing to do.
        return

    print("Patching vendored AsyncTCP::_accept (issue #107)...")
    for path in candidates:
        patch_file(path)


main()
