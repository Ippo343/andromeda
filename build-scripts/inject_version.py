#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This script runs 'git describe' to get the current version and injects it
into include/version.h as a C++ header file.

It also emits FIRMWARE_VERSION_CODE - a monotonically increasing integer
(the commit count) that OTA (#63) uses for "is the release newer than me?"
comparisons, since the git-describe string isn't ordered - and FIRMWARE_TAG,
the bare describe string without the branch suffix.
"""

Import("env")
import subprocess
import os
from datetime import datetime


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


def get_git_version_code():
    """Commit count on HEAD - a monotonic integer version for OTA comparisons.

    Linear (fast-forward-only) history is a project convention, so this only
    ever increases. Falls back to 0 if git is unavailable, which makes every
    published release look newer - safe, since OTA never auto-applies.
    """
    try:
        result = subprocess.run(
            ['git', 'rev-list', '--count', 'HEAD'],
            capture_output=True,
            text=True,
            check=True,
            cwd=env.get("PROJECT_DIR")
        )
        return int(result.stdout.strip())
    except (subprocess.CalledProcessError, ValueError):
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

// Monotonic integer (commit count). OTA (#63) compares this against a
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
    version_code = get_git_version_code()

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

    # Write the header file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"Successfully generated {output_path}")

main()