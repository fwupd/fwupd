#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import os
import sys
import unittest
from fwupd_test import FwupdTest, override_gi_search_path
import gi
import shutil
import subprocess

try:
    override_gi_search_path()
    gi.require_version("Fwupd", "2.0")
    from gi.repository import Fwupd  # pylint: disable=wrong-import-position
except ValueError:
    # when called from unittest-inspector this might not pass, we'll fail later
    # anyway in actual use
    pass


class UefiCapsuleTest(FwupdTest):
    #    def setUp(self):
    #        self.udisks, obj_udisks = self.spawn_server_template(
    #            "udisks2",
    #            {},
    #            stdout=subprocess.PIPE,
    #        )
    def test_uefi_capsule_device(self):
        if "TESTDATA" in os.environ:
            base = os.environ["TESTDATA"]
        else:
            base = os.path.dirname(__file__)
        target = os.path.join(self.testbed.get_root_dir(), "sys", "firmware")
        os.makedirs(target, exist_ok=True)
        shutil.copytree(os.path.join(base, "tests", "efi"), target, dirs_exist_ok=True)
        os.makedirs(os.path.join(target, "efi", "efivars"), exist_ok=True)
        target = os.path.join(target, "acpi", "tables")
        os.makedirs(target, exist_ok=True)
        shutil.copy(os.path.join(base, "tests", "acpi", "tables", "UEFI"), target)

        self.start_daemon()
        devices = Fwupd.Client().get_devices()
        for dev in devices:
            print(dev.to_string())


if __name__ == "__main__":
    # run ourselves under umockdev
    if "umockdev" not in os.environ.get("LD_PRELOAD", ""):
        os.execvp("umockdev-wrapper", ["umockdev-wrapper", sys.executable] + sys.argv)

    prog = unittest.main(exit=False)
    if prog.result.errors or prog.result.failures:
        sys.exit(1)

    # Translate to skip error
    if prog.result.testsRun == len(prog.result.skipped):
        sys.exit(77)
