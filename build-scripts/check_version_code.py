#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Release gate: refuse to publish a version code the fleet would ignore.

Not a PlatformIO extra_script - a plain CLI tool run by
.github/workflows/release.yml *before* it builds anything.

FIRMWARE_VERSION_CODE is the packed-semver number for the git tag
(build-scripts/version_code.py, #164), and the device decides whether an update
exists with a bare numeric compare:

    latest.versionCode > FIRMWARE_VERSION_CODE      # src/ota-updater.cpp

The packing is monotonic by construction, so an accidentally-lower code now
takes real effort - re-tagging an already-released version, or hand-editing the
manifest. But when it does happen the device evaluates that compare as false,
reports UpToDate, and never offers the update. Nothing errors: not the
workflow, not the device, not the web UI. The fleet simply sits on the broken
build with no in-band way out.

This turns that silent failure into a loud one at release time: a published
code must be strictly greater than every code already published, whatever
scheme produced it.
"""

import argparse
import json
import os
import subprocess
import sys


def is_releasable(new_code, published_code):
    """A release is only visible to a device already running published_code
    if its code is strictly greater. Equal is just as invisible as older."""
    return new_code > published_code


def newest_published_code(repo):
    """Highest versionCode across every published release's manifest.json.

    Scans all releases rather than just the latest: `gh release list` orders by
    creation date, and a stable release cut after a pre-release would otherwise
    hide the pre-release's higher code. Devices on the dev channel see both, so
    the bar is the highest code any of them could be running.

    Drafts are excluded. A device can't fetch a draft, so it sets no bar - and
    release.yml uploads to a draft before publishing, so a failed run can leave
    one behind. Counting it would block the retry of the very release that
    failed, with a message blaming the wrong thing.

    Returns 0 when the repo has no published releases yet (the first release,
    which trivially clears any bar).
    """
    gh_env = dict(os.environ, GH_REPO=repo)

    try:
        raw = subprocess.run(
            ["gh", "release", "list", "--limit", "100", "--json", "tagName,isDraft"],
            check=True, capture_output=True, text=True, env=gh_env,
        ).stdout
    except subprocess.CalledProcessError as e:
        sys.exit("check_version_code: could not list releases: %s" % e.stderr.strip())

    highest = 0
    for entry in json.loads(raw or "[]"):
        if entry.get("isDraft"):
            continue
        tag = entry["tagName"]
        try:
            manifest = subprocess.run(
                ["gh", "release", "download", tag, "--pattern", "manifest.json",
                 "--output", "-"],
                check=True, capture_output=True, text=True, env=gh_env,
            ).stdout
        except subprocess.CalledProcessError:
            # A release with no manifest.json predates OTA (#63), or is a
            # hand-made release. Nothing for a device to have installed from
            # it, so it sets no bar.
            continue

        for board in json.loads(manifest).get("boards", []):
            highest = max(highest, int(board.get("versionCode", 0)))

    return highest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True, help="owner/repo, e.g. Ippo343/andromeda")
    ap.add_argument("--version-code", required=True, type=int,
                    help="the code this release would publish")
    ap.add_argument("--tag", required=True, help="the tag being released, for the message")
    args = ap.parse_args()

    published = newest_published_code(args.repo)

    if is_releasable(args.version_code, published):
        print("version-code gate OK: %s publishes code %d, newest published is %d"
              % (args.tag, args.version_code, published))
        return

    sys.exit(
        "\n".join([
            "",
            "*** VERSION-CODE GATE FAILED",
            "*** %s would publish version code %d, but code %d is already published."
            % (args.tag, args.version_code, published),
            "***",
            "*** Devices decide with `latest.versionCode > FIRMWARE_VERSION_CODE`, so this",
            "*** release would be INVISIBLE to the fleet - reported as UpToDate, never",
            "*** offered, with no error anywhere. Publishing it would strand every device",
            "*** on whatever it is running now.",
            "***",
            "*** The code is the packed MAJOR.MINOR.PATCH of this tag (build-scripts/",
            "*** version_code.py). Bump the version number in the tag so it sorts above the",
            "*** newest release - a hot-fix of vX.Y.Z ships as vX.Y.(Z+1), not a re-tag.",
            "",
        ])
    )


if __name__ == "__main__":
    main()
