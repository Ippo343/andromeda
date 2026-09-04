#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Pins the packed-semver scheme in build-scripts/version_code.py (#164).

Run in CI's native-tests job (python -m unittest discover -s build-scripts) and
locally the same way, or directly: `python build-scripts/test_version_code.py`.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from version_code import code_for_tag  # noqa: E402


class ExactValues(unittest.TestCase):
    def test_final_release(self):
        self.assertEqual(code_for_tag("v1.2.3"), 10203999)

    def test_patch_defaults_to_zero(self):
        self.assertEqual(code_for_tag("v1.2"), 10200999)

    def test_prerelease_stages(self):
        self.assertEqual(code_for_tag("v1.2.3-dev"), 10203000)
        self.assertEqual(code_for_tag("v1.2.3-alpha"), 10203100)
        self.assertEqual(code_for_tag("v1.2.3-alpha-2"), 10203102)
        self.assertEqual(code_for_tag("v1.2.3-beta"), 10203300)
        self.assertEqual(code_for_tag("v1.2.3-rc1"), 10203501)
        self.assertEqual(code_for_tag("v1.2.3-rc2"), 10203502)

    def test_iteration_is_clamped(self):
        self.assertEqual(code_for_tag("v1.2.3-beta-250"), 10203399)


class Ordering(unittest.TestCase):
    def test_prereleases_ascend_to_final(self):
        codes = [
            code_for_tag(t)
            for t in [
                "v1.2.3-dev",
                "v1.2.3-alpha",
                "v1.2.3-alpha-2",
                "v1.2.3-beta",
                "v1.2.3-rc1",
                "v1.2.3-rc2",
                "v1.2.3",
            ]
        ]
        self.assertEqual(codes, sorted(codes))
        self.assertEqual(len(set(codes)), len(codes))

    def test_numeric_not_lexical_minor_bump(self):
        self.assertGreater(code_for_tag("v1.10.0"), code_for_tag("v1.9.0"))

    def test_major_bump_beats_high_minor_patch(self):
        self.assertGreater(code_for_tag("v2.0.0"), code_for_tag("v1.99.99"))

    def test_any_prerelease_outranks_previous_final(self):
        self.assertGreater(code_for_tag("v1.2.3-dev"), code_for_tag("v1.2.2"))


class CodenameAndDescribeForms(unittest.TestCase):
    def test_codename_is_ignored(self):
        self.assertEqual(code_for_tag("v0.9-deep-space-network"), 900999)

    def test_codename_with_prerelease_suffix(self):
        self.assertEqual(code_for_tag("v0.9-deep-space-network-rc1"), 900501)

    def test_git_describe_trailer_is_stripped(self):
        self.assertEqual(
            code_for_tag("v0.9-deep-space-network-12-ge7b270a"), 900999
        )

    def test_dirty_suffix_is_stripped(self):
        self.assertEqual(code_for_tag("v0.9-deep-space-network-dirty"), 900999)
        self.assertEqual(
            code_for_tag("v0.9-deep-space-network-12-ge7b270a-dirty"), 900999
        )


class MigrationConstraint(unittest.TestCase):
    # Highest versionCode published to the fleet today is 493; the local commit
    # count on main is 505. The first tag on the new scheme must clear both.
    def test_first_new_release_clears_the_deployed_bar(self):
        self.assertGreater(code_for_tag("v1.0.0"), 505)

    def test_even_a_z_bump_hotfix_clears_the_bar(self):
        self.assertGreater(code_for_tag("v0.9.1"), 505)


class Rejects(unittest.TestCase):
    def test_non_version_tag(self):
        with self.assertRaises(ValueError):
            code_for_tag("not-a-tag")

    def test_minor_overflow(self):
        with self.assertRaises(ValueError):
            code_for_tag("v1.100.0")

    def test_patch_overflow(self):
        with self.assertRaises(ValueError):
            code_for_tag("v1.0.100")


if __name__ == "__main__":
    unittest.main()
