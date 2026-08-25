#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
env:native_runtime only: statically links the MinGW runtime (libstdc++,
libgcc, libwinpthread) into program.exe.

Unlike env:native (only ever run by `pio test`, which resolves its own MSYS2
toolchain PATH), this binary is meant to be spawned directly by
tools/native-bridge/server.js - a plain Node child_process.spawn() call with
no guarantee MSYS2's bin dir is on PATH. Without this, the exe fails to load
with exit code 0xC0000139/0xC0000135 (DLL/entry point not found) outside a
shell that happens to have MSYS2 on PATH.

A plain `-static` build_flag doesn't work here: PlatformIO's native platform
only forwards build_flags to the per-file compile steps, not the final link
command (confirmed via `pio run -v`), so this has to go on LINKFLAGS
directly via the SCons env instead.
"""

Import("env")

env.Append(LINKFLAGS=["-static"])
