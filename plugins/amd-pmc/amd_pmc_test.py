#!/usr/bin/env python3
#
# Copyright 2024 Mario Limonciello <mario.limonciello@amd.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import os
import sys
import unittest
from fwupd_test import FwupdTest, override_gi_search_path
import gi

try:
    override_gi_search_path()
    gi.require_version("Fwupd", "2.0")
    from gi.repository import Fwupd  # pylint: disable=wrong-import-position
except ValueError:
    # when called from unittest-inspector this might not pass, we'll fail later
    # anyway in actual use
    pass


class AmdPmcTest(FwupdTest):
    def test_amd_pmc_device(self):
        """Verify the AMD PMC device is detected correctly"""

        self.testbed.add_from_string(
            """P: /devices/platform/AMDI0009:00
E: DRIVER=amd_pmc
E: ID_PATH=platform-AMDI0009:00
E: ID_PATH_TAG=platform-AMDI0009_00
E: ID_VENDOR_FROM_DATABASE=Amdek Corporation
E: MODALIAS=acpi:AMDI0009:PNP0D80:
E: SUBSYSTEM=platform
L: driver=../../../bus/platform/drivers/amd_pmc
A: driver_override=(null)
L: firmware_node=../../LNXSYSTM:00/LNXSYBUS:00/AMDI0009:00
A: modalias=acpi:AMDI0009:PNP0D80:
A: power/control=auto
A: power/runtime_active_time=0
A: power/runtime_status=unsupported
A: power/runtime_suspended_time=0
A: smu_fw_version=76.78.0
A: smu_program=0
"""
        )

        self.start_daemon()
        devices = Fwupd.Client().get_devices()
        count = 0
        for dev in devices:
            if dev.get_plugin() != "amd_pmc":
                continue
            self.assertEqual(dev.get_name(), "System Management Unit (SMU)")
            self.assertEqual(dev.get_vendor(), "Advanced Micro Devices, Inc.")
            self.assertEqual(dev.get_version(), "76.78.0")
            count += 1
        self.assertEqual(count, 1)

    def test_amd_pmc_old_kernel(self):
        """Verify the behavior of amd-pmc plugin with an older kernel"""

        self.testbed.add_from_string(
            """P: /devices/platform/AMDI0009:00
E: DRIVER=amd_pmc
E: ID_PATH=platform-AMDI0009:00
E: ID_PATH_TAG=platform-AMDI0009_00
E: ID_VENDOR_FROM_DATABASE=Amdek Corporation
E: MODALIAS=acpi:AMDI0009:PNP0D80:
E: SUBSYSTEM=platform
L: driver=../../../bus/platform/drivers/amd_pmc
A: driver_override=(null)
L: firmware_node=../../LNXSYSTM:00/LNXSYBUS:00/AMDI0009:00
A: modalias=acpi:AMDI0009:PNP0D80:
A: power/control=auto
A: power/runtime_active_time=0
A: power/runtime_status=unsupported
A: power/runtime_suspended_time=0
"""
        )
        self.start_daemon()
        devices = Fwupd.Client().get_devices()
        for dev in devices:
            self.assertNotEqual(dev.get_plugin(), "amd_pmc")

    def test_amd_pmc_broken_kernel(self):
        """Verify the behavior of amd-pmc plugin with a kernel that advertises SMU FW version but not program"""

        self.testbed.add_from_string(
            """P: /devices/platform/AMDI0009:00
E: DRIVER=amd_pmc
E: ID_PATH=platform-AMDI0009:00
E: ID_PATH_TAG=platform-AMDI0009_00
E: ID_VENDOR_FROM_DATABASE=Amdek Corporation
E: MODALIAS=acpi:AMDI0009:PNP0D80:
E: SUBSYSTEM=platform
L: driver=../../../bus/platform/drivers/amd_pmc
A: driver_override=(null)
L: firmware_node=../../LNXSYSTM:00/LNXSYBUS:00/AMDI0009:00
A: modalias=acpi:AMDI0009:PNP0D80:
A: power/control=auto
A: power/runtime_active_time=0
A: power/runtime_status=unsupported
A: power/runtime_suspended_time=0
A: smu_fw_version=76.78.0
"""
        )
        self.start_daemon()
        devices = Fwupd.Client().get_devices()
        for dev in devices:
            self.assertNotEqual(dev.get_plugin(), "amd_pmc")


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
