"""Post-build guard: the vendored AsyncTCP's accept trampoline must NULL-check
the pcb before constructing an AsyncClient from it.

Issue #107: the controller panic'ed with a "Load access fault" because the
stale AsyncTCP that lacamera/ESPAsyncWebServer#3.1.0 vendored ran
`new AsyncClient(pcb)` in its accept callback without checking `pcb`, and lwIP
does hand that callback a NULL pcb under accept-backlog pressure (out of TCP
PCBs, or an RST between the SYN and the callback). The fix was to move to the
maintained ESP32Async/AsyncTCP, whose accept trampoline bails early on a NULL
pcb.

That guard is the whole reason for the library swap, and it lives in
third-party source we pin by version - a careless bump (or a stray return to
an unguarded fork) would silently reintroduce the crash with every other test
layer still green. This runs after each hardware link, where the library is
resolved under .pio/libdeps/<env>/, and fails the build if the accept
trampoline is present but unguarded.

Whether an env is expected to have AsyncTCP is read from its lib_deps, not
inferred from whether the source was found: an env that declares the dependency
but whose AsyncTCP.cpp can't be located fails the build, because "the library
moved" and "this env doesn't use the library" must not look the same. Envs that
genuinely don't pull it (native/native_runtime) print an explicit
ACCEPT_GUARD_CHECK_SKIPPED line, so a silently-disabled guard is greppable in
the build log.

If the *function* can't be located inside a source that was found (a future
upstream rename), this warns loudly but does not fail - the compile itself is
the real signal, and a heuristic shouldn't hard-block a hardware build on its
own.
"""

import glob
import os
import re
import sys

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

# The accept trampoline across fork revisions: me-no-dev / esphome named it
# `int8_t AsyncServer::_accept(tcp_pcb* pcb, int8_t err)`; ESP32Async 3.5+
# renamed it `int8_t AsyncTCP_detail::tcp_accept(void* arg, tcp_pcb* pcb,
# int8_t err)`. Match either: an int8_t-returning free/member function whose
# args include a `tcp_pcb *` and an `int8_t`, named _accept or tcp_accept.
_OPEN_RE = re.compile(
    r"int8_t\s+(?:\w+::)?(?:_accept|tcp_accept)\s*\([^)]*\btcp_pcb\s*\*[^)]*\bint8_t\b[^)]*\)\s*\{"
)

# A NULL-pcb bail: `if (!pcb)`, `if (pcb == NULL)`, `if (pcb == nullptr)`,
# `if (NULL == pcb)`, with or without a `|| err != ERR_OK` tail.
_GUARD_RE = re.compile(
    r"if\s*\(\s*(?:!\s*pcb|pcb\s*==\s*(?:NULL|nullptr|0)|(?:NULL|nullptr)\s*==\s*pcb)"
)

# Constructing the client from the pcb - the deref that panics on NULL.
_DEREF_RE = re.compile(r"new\b[^;{]*AsyncClient\s*\(\s*pcb\b|AsyncClient\s+\w+\s*\(\s*pcb\b")


def _find_function_body(src, open_match):
    """Return the {...} body of the function whose opening brace open_match ends on."""
    start = open_match.end() - 1  # index of the '{'
    depth = 0
    for i in range(start, len(src)):
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return src[start : i + 1]
    return src[start:]  # unbalanced - shouldn't happen, check what we have


def _check_source(path):
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        src = f.read()

    open_match = _OPEN_RE.search(src)
    if not open_match:
        print("")
        print("*** AsyncTCP accept-guard check: accept trampoline not found in")
        print("*** " + path)
        print("*** (upstream may have renamed it) - skipping, NOT failing the build.")
        print("*** Re-check the #107 fix by hand and update build-scripts/"
              "check_asynctcp_accept_guard.py.")
        print("")
        return True

    body = _find_function_body(src, open_match)

    guard = _GUARD_RE.search(body)
    deref = _DEREF_RE.search(body)

    if guard is None or (deref is not None and deref.start() < guard.start()):
        print("")
        print("*** ASYNCTCP ACCEPT-GUARD CHECK FAILED (issue #107)")
        print("*** " + path)
        print("*** The accept trampoline constructs an AsyncClient from `pcb`")
        print("*** without a preceding `if (!pcb)` bail. lwIP can call it with a")
        print("*** NULL pcb -> load-access-fault panic. This is exactly the crash")
        print("*** the ESP32Async library swap was meant to fix - check the")
        print("*** AsyncTCP version pinned in platformio.ini.")
        print("")
        return False

    print("AsyncTCP accept-guard check OK: NULL-pcb bail present in "
          + os.path.basename(os.path.dirname(os.path.dirname(path))))
    return True


def _expects_asynctcp(env):
    """Does this env actually declare AsyncTCP as a dependency?

    The check used to infer this from whether the glob found anything, which
    made "the library moved" and "this env doesn't use the library"
    indistinguishable - so a lib_deps rename, an upstream library.json name
    change, or a half-populated libdeps dir silently switched the #107 crash
    gate off with every test layer still green, including on the release build.
    lib_deps is the declaration of intent; ask it instead.
    """
    try:
        deps = env.GetProjectOption("lib_deps", [])
    except Exception:
        return False
    if isinstance(deps, str):
        deps = [deps]
    return any("asynctcp" in str(d).lower() for d in deps)


def check_asynctcp_accept_guard(source, target, env):
    libdeps = env.subst("$PROJECT_LIBDEPS_DIR")
    pioenv = env.subst("$PIOENV")
    roots = [
        os.path.join(libdeps, pioenv, "AsyncTCP", "src", "AsyncTCP.cpp"),
        # older forks, just in case a stale dir lingers
        os.path.join(libdeps, pioenv, "AsyncTCP*", "src", "AsyncTCP.cpp"),
    ]
    seen = set()
    candidates = []
    for pattern in roots:
        for path in glob.glob(pattern):
            if path not in seen and os.path.isfile(path):
                seen.add(path)
                candidates.append(path)

    if not candidates:
        if not _expects_asynctcp(env):
            # Native envs and anything else that genuinely doesn't pull
            # AsyncTCP. Say so out loud - a skip that prints nothing is
            # indistinguishable from a check that passed.
            print("ACCEPT_GUARD_CHECK_SKIPPED: %s does not depend on AsyncTCP" % pioenv)
            return

        print("")
        print("*** ASYNCTCP ACCEPT-GUARD CHECK COULD NOT RUN (issue #107)")
        print("*** %s declares AsyncTCP in lib_deps, but no AsyncTCP.cpp was found under" % pioenv)
        print("*** " + os.path.join(libdeps, pioenv))
        print("*** so the NULL-pcb guard was never verified. This build could contain the")
        print("*** crash the library swap exists to prevent. Failing rather than passing")
        print("*** silently: check the lib_deps spec and that libdeps is fully resolved.")
        print("")
        sys.exit(1)

    ok = all(_check_source(path) for path in candidates)
    if not ok:
        sys.exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_asynctcp_accept_guard)  # noqa: F821
