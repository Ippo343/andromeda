#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Packed-semver firmware version code - the single source of truth for #164.

The device decides "is there an update?" with a bare int compare
(`latest.versionCode > FIRMWARE_VERSION_CODE`, src/ota-updater.cpp). The number
that feeds it used to be `git rev-list --count`, which is only monotonic along
one line of history: a tag cut off a side branch or an older commit - exactly
what a hot-fix for a bad release is - can produce a code the fleet has already
passed, and the update is then silently never offered.

This derives the code from the git *tag* instead, packed so it is monotonic by
construction and still legible (support can read a `/metrics` code and know the
firmware):

    core = MAJOR*10000 + MINOR*100 + PATCH     # MINOR, PATCH each < 100
    code = core*1000 + rank                    # rank in [0, 999]

`rank` orders pre-releases below their final: a final release is 999; a
pre-release is `stage_base + min(iter, 99)` with dev=0, alpha=100, beta=300,
rc=500. So for v1.2.3:

    -dev   10203000
    -alpha 10203100   -alpha-2 10203102
    -beta  10203300
    -rc1   10203501   -rc2 10203502
    (final) 10203999

Any pre-release of v1.2.3 still outranks final v1.2.2 (10202999).

Consumers: build-scripts/inject_version.py (bakes FIRMWARE_VERSION_CODE into
include/version.h) and .github/workflows/release.yml (stamps the manifest's
versionCode) - both must agree for the same tag, hence one implementation.
build-scripts/check_version_code.py stays a separate release-time backstop
against a non-increasing *published* code.
"""

import re
import sys

# Pre-release stage -> its rank floor. A final release sits above all of them.
_STAGE_BASE = {"dev": 0, "alpha": 100, "beta": 300, "rc": 500}
_FINAL_RANK = 999

# Leading vMAJOR.MINOR[.PATCH]; a trailing "-codename" and/or pre-release suffix
# are matched separately below.
_CORE_RE = re.compile(r"^v(\d+)\.(\d+)(?:\.(\d+))?")

# Pre-release suffix, anchored at the end. `rcN` carries its number attached;
# dev/alpha/beta take it as a "-N" iteration. This mirrors the channel regex in
# release.yml: -(dev|alpha|beta|rc[0-9]*)(-[0-9]+)?$
_PRERELEASE_RE = re.compile(r"-(dev|alpha|beta|rc)(\d*)(?:-(\d+))?$")

# `git describe --tags --long`-style trailer: "-<commits>-g<abbrev>", optionally
# "-dirty". Stripped so a dev checkout packs the same code as its base tag.
_DESCRIBE_TRAILER_RE = re.compile(r"-\d+-g[0-9a-f]+$")


def code_for_tag(tag_or_describe):
    """Pack a git tag (or `git describe` string) into a monotonic int.

    Raises ValueError for anything that does not start with `vMAJOR.MINOR`, or
    whose MINOR/PATCH would not fit the two decimal digits the packing reserves.
    """
    s = tag_or_describe.strip()
    if s.endswith("-dirty"):
        s = s[: -len("-dirty")]
    s = _DESCRIBE_TRAILER_RE.sub("", s)

    m = _CORE_RE.match(s)
    if not m:
        raise ValueError("not a version tag: %r" % tag_or_describe)

    major = int(m.group(1))
    minor = int(m.group(2))
    patch = int(m.group(3)) if m.group(3) else 0
    if minor >= 100 or patch >= 100:
        raise ValueError(
            "MINOR and PATCH must each be < 100 for packing: %r" % tag_or_describe
        )

    pre = _PRERELEASE_RE.search(s[m.end():])
    if pre is None:
        rank = _FINAL_RANK
    else:
        stage = pre.group(1)
        iteration = int(pre.group(2) or pre.group(3) or 0)
        rank = _STAGE_BASE[stage] + min(iteration, 99)

    core = major * 10000 + minor * 100 + patch
    return core * 1000 + rank


def main(argv):
    if len(argv) != 2:
        sys.exit("usage: version_code.py <git-tag-or-describe-string>")
    try:
        print(code_for_tag(argv[1]))
    except ValueError as e:
        sys.exit("version_code: %s" % e)


if __name__ == "__main__":
    main(sys.argv)
