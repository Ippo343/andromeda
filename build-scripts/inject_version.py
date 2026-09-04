#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This script runs 'git describe' to get the current version and injects it
into include/version.h as a C++ header file.

It also emits FIRMWARE_VERSION_CODE - a monotonically increasing integer that
OTA (#63) uses for "is the release newer than me?" comparisons, since the
git-describe string isn't ordered - and FIRMWARE_TAG, the bare describe string
without the branch suffix.

FIRMWARE_VERSION_CODE is the packed-semver number derived from the git tag by
build-scripts/version_code.py (#164). It used to be `git rev-list --count`,
which is only monotonic along one line of history - a tag off a side branch or
an older commit could produce a code the fleet had already passed, stranding it.
"""

Import("env")
import subprocess
import os
import sys
from datetime import datetime

# SCons exec()s this script, so __file__ is undefined here - locate the sibling
# module via the project dir instead.
sys.path.insert(0, os.path.join(env.get("PROJECT_DIR"), "build-scripts"))
from version_code import code_for_tag


def get_git_version():
    """Get version string from git describe."""
    try:
        # Try to get git describe output
        result = subprocess.run(
            ['git', 'describe', '--tags', '--always', '--dirty'],
            capture_output=True,
            text=True,
            check=True,
            cwd=env.get("PROJECT_DIR")
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        # If git describe fails (no tags), try just the commit hash
        try:
            result = subprocess.run(
                ['git', 'rev-parse', '--short', 'HEAD'],
                capture_output=True,
                text=True,
                check=True,
                cwd=env.get("PROJECT_DIR")
            )
            return f"0.0.0-g{result.stdout.strip()}"
        except subprocess.CalledProcessError:
            # If git isn't available or not a git repo
            return "unknown"


def get_git_version_code(version):
    """Packed-semver code (build-scripts/version_code.py) for the current tag.

    `version` is the `git describe` string from get_git_version(); its
    "-<n>-g<hash>" / "-dirty" trailer is stripped by code_for_tag, so a dev
    checkout packs the same code as its base tag. That is imprecise between
    tags but safe: OTA only ever compares against a *published* manifest, so a
    dev box still sees any real newer release as newer.

    Falls back to 0 when git is unavailable or the string doesn't parse
    (e.g. a repo with no tags), which makes every published release look newer
    - safe, since OTA never auto-applies.
    """
    try:
        return code_for_tag(version)
    except ValueError:
        return 0


def get_git_branch():
    """Get current git branch name."""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
            capture_output=True,
            text=True,
            check=True,
            cwd=env.get("PROJECT_DIR")
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        return "unknown"


def generate_version_header(version, branch, version_code):
    """Generate the version.h header file content."""

    version_string = f"{version} ({branch})"

    header = f"""// Auto-generated version header
// Generated at build time by generate_version.py
// DO NOT EDIT MANUALLY

#ifndef VERSION_H
#define VERSION_H

#define VERSION "{version_string}"

// Bare `git describe` string (no branch suffix) - display only.
#define FIRMWARE_TAG "{version}"

// Monotonic integer: the packed-semver number for this build's git tag
// (build-scripts/version_code.py, #164). OTA (#63) compares this against a
// release manifest's versionCode to decide whether an update is newer.
#define FIRMWARE_VERSION_CODE {version_code}

#endif // VERSION_H
"""
    return header


def main():
    print("Generating version header from git...")

    # Get version information
    version = get_git_version()
    branch = get_git_branch()
    version_code = get_git_version_code(version)

    print(f"  Version: {version}")
    print(f"  Branch: {branch}")
    print(f"  Version code: {version_code}")

    # Generate header content
    header_content = generate_version_header(version, branch, version_code)

    # Determine output path
    project_dir = env.get("PROJECT_DIR")
    output_path = os.path.join(project_dir, "include", "version.h")

    # Ensure include directory exists
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    # Skip the write if the content hasn't changed. Not primarily a speed
    # optimization (the git calls above are ~0.1s total) - it matters because
    # a parallel multi-env build (build-scripts/build-all.ps1) runs this
    # script once per env, all writing the same path concurrently. An
    # unconditional write is a real race between those processes; a
    # read-compare-skip makes a no-op run genuinely a no-op.
    try:
        with open(output_path, 'r', encoding='utf-8') as f:
            if f.read() == header_content:
                print(f"{output_path} already up to date")
                return
    except FileNotFoundError:
        pass

    # Write the header file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"Successfully generated {output_path}")

main()