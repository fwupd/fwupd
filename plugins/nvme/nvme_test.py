#!/usr/bin/env python3
#
# Copyright 2024 Mario Limonciello <mario.limonciello@amd.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import gi
import os
import sys
import unittest
from fwupd_test import FwupdTest, override_gi_search_path

gi.require_version("UMockdev", "1.0")
from gi.repository import UMockdev

try:
    override_gi_search_path()
    gi.require_version("Fwupd", "2.0")
    from gi.repository import Fwupd  # pylint: disable=wrong-import-position
except ValueError:
    # when called from unittest-inspector this might not pass, we'll fail later
    # anyway in actual use
    pass

try:
    from nvme_ioctl import handle_ioctl
except ImportError:
    # when called from unittest-inspector this might not pass, we'll fail later
    # anyway in actual use
    pass


class NmveTest(FwupdTest):
    def setUp(self):
        super().setUp()
        self.testbed.add_from_string(
            """P: /devices/pci0000:00/0000:00:02.4/0000:02:00.0/nvme/nvme0
N: nvme0
E: DEVNAME=/dev/nvme0
E: ID_MODEL_FROM_DATABASE=WD Black SN770 / PC SN740 256GB / PC SN560 (DRAM-less) NVMe SSD
E: ID_PCI_CLASS_FROM_DATABASE=Mass storage controller
E: ID_PCI_INTERFACE_FROM_DATABASE=NVM Express
E: ID_PCI_SUBCLASS_FROM_DATABASE=Non-Volatile memory controller
E: ID_VENDOR_FROM_DATABASE=Sandisk Corp
E: MAJOR=239
E: MINOR=0
E: NVME_TRTYPE=pcie
E: SUBSYSTEM=nvme
A: address=0000:02:00.0
A: cntlid=0
A: cntrltype=io
A: dctype=none
A: dev=239:0
L: device=../../../0000:02:00.0
A: firmware_rev=73110000
A: kato=0
A: model=WD PC SN740 SDDPNQD-256G
A: numa_node=-1
A: passthru_err_log_enabled=off
A: queue_count=17
A: serial=223361440214
A: sqsize=1023
A: state=live
A: subsysnqn=nqn.2018-01.com.wdc:guid:E8238FA6BF53-0001-001B444A488DC60A
A: transport=pcie

"""
        )
        self.testbed.add_from_string(
            """P: /devices/pci0000:00/0000:00:02.4/0000:02:00.0
E: DRIVER=nvme
E: ID_MODEL_FROM_DATABASE=WD Black SN770 / PC SN740 256GB / PC SN560 (DRAM-less) NVMe SSD
E: ID_PATH=pci-0000:02:00.0
E: ID_PATH_TAG=pci-0000_02_00_0
E: ID_PCI_CLASS_FROM_DATABASE=Mass storage controller
E: ID_PCI_INTERFACE_FROM_DATABASE=NVM Express
E: ID_PCI_SUBCLASS_FROM_DATABASE=Non-Volatile memory controller
E: ID_VENDOR_FROM_DATABASE=Sandisk Corp
E: MODALIAS=pci:v000015B7d00005017sv000015B7sd00005017bc01sc08i02
E: PCI_CLASS=10802
E: PCI_ID=15B7:5017
E: PCI_SLOT_NAME=0000:02:00.0
E: PCI_SUBSYS_ID=15B7:5017
E: SUBSYSTEM=pci
A: aer_dev_correctable=RxErr 0BadTLP 0BadDLLP 0Rollover 0Timeout 0NonFatalErr 0CorrIntErr 0HeaderOF 0TOTAL_ERR_COR 0
A: aer_dev_fatal=Undefined 0DLP 0SDES 0TLP 0FCP 0CmpltTO 0CmpltAbrt 0UnxCmplt 0RxOF 0MalfTLP 0ECRC 0UnsupReq 0ACSViol 0UncorrIntErr 0BlockedTLP 0AtomicOpBlocked 0TLPBlockedErr 0PoisonTLPBlocked 0TOTAL_ERR_FATAL 0
A: aer_dev_nonfatal=Undefined 0DLP 0SDES 0TLP 0FCP 0CmpltTO 0CmpltAbrt 0UnxCmplt 0RxOF 0MalfTLP 0ECRC 0UnsupReq 0ACSViol 0UncorrIntErr 0BlockedTLP 0AtomicOpBlocked 0TLPBlockedErr 0PoisonTLPBlocked 0TOTAL_ERR_NONFATAL 0
A: ari_enabled=0
A: broken_parity_status=0
A: class=0x010802
H: config=B71517500704100001020801100000000400A090000000000000000000000000000000000000000000000000B7151750000000008000000000000000FF010000
A: consistent_dma_mask_bits=64
A: current_link_speed=16.0 GT/s PCIe
A: current_link_width=4
A: d3cold_allowed=1
A: device=0x5017
A: dma_mask_bits=64
L: driver=../../../../bus/pci/drivers/nvme
A: driver_override=(null)
A: enable=1
L: firmware_node=../../../LNXSYSTM:00/LNXSYBUS:00/PNP0A08:00/device:0e/device:0f
A: link/clkpm=1
A: link/l1_1_pcipm=1
A: link/l1_2_aspm=1
A: link/l1_2_pcipm=1
A: link/l1_aspm=1
A: local_cpulist=0-11
A: local_cpus=0fff
A: max_link_speed=16.0 GT/s PCIe
A: max_link_width=4
A: modalias=pci:v000015B7d00005017sv000015B7sd00005017bc01sc08i02
A: pools=poolinfo - 0.1prp list 256        0  384  256 24prp list page       0    0 4096  0
A: power_state=D0
A: reset_method=flr bus
A: resource=0x0000000090a00000 0x0000000090a03fff 0x00000000001402040x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x0000000000000000
A: revision=0x01
A: subsystem_device=0x5017
A: subsystem_vendor=0x15b7
A: vendor=0x15b7
"""
        )

        self.testbed.add_from_string(
            """P: /devices/pci0000:00/0000:00:02.4
E: DRIVER=pcieport
E: ID_PATH=pci-0000:00:02.4
E: ID_PATH_TAG=pci-0000_00_02_4
E: ID_PCI_CLASS_FROM_DATABASE=Bridge
E: ID_PCI_INTERFACE_FROM_DATABASE=Normal decode
E: ID_PCI_SUBCLASS_FROM_DATABASE=PCI bridge
E: ID_VENDOR_FROM_DATABASE=Advanced Micro Devices, Inc. [AMD]
E: MODALIAS=pci:v00001022d000014EEsv00001022sd00001453bc06sc04i00
E: PCI_CLASS=60400
E: PCI_ID=1022:14EE
E: PCI_SLOT_NAME=0000:00:02.4
E: PCI_SUBSYS_ID=1022:1453
E: SUBSYSTEM=pci
A: ari_enabled=0
A: broken_parity_status=0
A: class=0x060400
H: config=2210EE14070410000000040610008100000000000000000000020200F1010000A090A090F1FF01000000000000000000000000005000000000000000FF000200
A: consistent_dma_mask_bits=32
A: current_link_speed=16.0 GT/s PCIe
A: current_link_width=4
A: d3cold_allowed=1
A: device=0x14ee
A: dma_mask_bits=32
L: driver=../../../bus/pci/drivers/pcieport
A: driver_override=(null)
A: enable=1
L: firmware_node=../../LNXSYSTM:00/LNXSYBUS:00/PNP0A08:00/device:0e
A: local_cpulist=0-11
A: local_cpus=0fff
A: max_link_speed=16.0 GT/s PCIe
A: max_link_width=4
A: modalias=pci:v00001022d000014EEsv00001022sd00001453bc06sc04i00
A: power_state=D0
A: reset_method=pm
A: resource=0x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000090a00000 0x0000000090afffff 0x00000000000002000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x0000000000000000
A: revision=0x00
A: secondary_bus_number=2
A: subordinate_bus_number=2
A: subsystem_device=0x1453
A: subsystem_vendor=0x1022
A: vendor=0x1022
"""
        )
        # handler for the IOCTL
        handler = UMockdev.IoctlBase()
        handler.connect("handle-ioctl", handle_ioctl)
        self.testbed.attach_ioctl("/dev/nvme0", handler)

    def tearDown(self):
        self.testbed.detach_ioctl("/dev/nvme0")
        super().tearDown()

    def test_nvme_device(self):
        self.start_daemon()
        devices = Fwupd.Client().get_devices()

        count = 0
        for dev in devices:
            if dev.get_plugin() != "nvme":
                continue
            count += 1
            self.assertEqual(
                dev.get_flags(),
                1
                | Fwupd.DeviceFlags.UPDATABLE
                | Fwupd.DeviceFlags.INTERNAL
                | Fwupd.DeviceFlags.REQUIRE_AC
                | Fwupd.DeviceFlags.NEEDS_REBOOT
                | Fwupd.DeviceFlags.USABLE_DURING_UPDATE,
            )
            self.assertEqual(dev.get_name(), "WD PC SN740 SDDPNQD-256G")
            self.assertEqual(dev.get_vendor(), "Sandisk Corp")
            self.assertEqual(dev.get_version(), "73110000")
            self.assertEqual(dev.get_serial(), "223361440214")
            guids = dev.get_guids()
            self.assertEqual(len(guids), 3)
            for guid in [
                "1524d43d-ed91-5130-8cb6-8b8478508bae",
                "87cfda90-ce08-52c3-9bb5-0e0718b7e57e",
                "89ef8df4-48de-5d63-b540-59371a6dce36",
            ]:
                self.assertIn(guid, guids)
            instance_ids = dev.get_instance_ids()
            self.assertEqual(len(instance_ids), 3)
            for instance in [
                "NVME\\VEN_15B7&DEV_5017",
                "NVME\\VEN_15B7&DEV_5017&SUBSYS_15B75017",
                "WD PC SN740 SDDPNQD-256G",
            ]:
                self.assertIn(instance, instance_ids)
        self.assertGreater(count, 0)


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
