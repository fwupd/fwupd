#!/usr/bin/env python3
#
# Copyright 2024 Mario Limonciello <mario.limonciello@amd.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import gi
import os
import sys
import unittest
from fwupd_test import FwupdTest

try:
    gi.require_version("Fwupd", "2.0")
    from gi.repository import Fwupd  # pylint: disable=wrong-import-position
except ValueError:
    # when called from unittest-inspector this might not pass, we'll fail later
    # anyway in actual use
    pass


class PciPspNoHsi(FwupdTest):
    """A system that doesn't support exporting any HSI attributes or versions"""

    def setUp(self):
        super().setUp()
        self.testbed.add_from_string(
            """P: /devices/pci0000:20/0000:20:08.1/0000:23:00.1
E: DRIVER=ccp
E: PCI_CLASS=108000
E: PCI_ID=1022:1486
E: PCI_SUBSYS_ID=17AA:1046
E: PCI_SLOT_NAME=0000:23:00.1
E: MODALIAS=pci:v00001022d00001486sv000017AAsd00001046bc10sc80i00
E: SUBSYSTEM=pci
E: ID_PCI_CLASS_FROM_DATABASE=Encryption controller
E: ID_PCI_SUBCLASS_FROM_DATABASE=Encryption controller
E: ID_VENDOR_FROM_DATABASE=Advanced Micro Devices, Inc. [AMD]
E: ID_MODEL_FROM_DATABASE=Starship/Matisse Cryptographic Coprocessor PSPCPP
A: aer_dev_correctable=RxErr 0BadTLP 0BadDLLP 0Rollover 0Timeout 0NonFatalErr 0CorrIntErr 0HeaderOF 0TOTAL_ERR_COR 0
A: aer_dev_fatal=Undefined 0DLP 0SDES 0TLP 0FCP 0CmpltTO 0CmpltAbrt 0UnxCmplt 0RxOF 0MalfTLP 0ECRC 0UnsupReq 0ACSViol 0UncorrIntErr 0BlockedTLP 0AtomicOpBlocked 0TLPBlockedErr 0PoisonTLPBlocked 0TOTAL_ERR_FATAL 0
A: aer_dev_nonfatal=Undefined 0DLP 0SDES 0TLP 0FCP 0CmpltTO 0CmpltAbrt 0UnxCmplt 0RxOF 0MalfTLP 0ECRC 0UnsupReq 0ACSViol 0UncorrIntErr 0BlockedTLP 0AtomicOpBlocked 0TLPBlockedErr 0PoisonTLPBlocked 0TOTAL_ERR_NONFATAL 0
A: ari_enabled=0
A: broken_parity_status=0
A: class=0x108000
H: config=2210861406041000000080101000800000000000000000000000D0C300000000000000000000E0C300000000AA174610000000004800000000000000FF010000
A: consistent_dma_mask_bits=48
A: current_link_speed=16.0 GT/s PCIe
A: current_link_width=16
A: d3cold_allowed=1
A: device=0x1486
A: dma_mask_bits=48
L: driver=../../../../bus/pci/drivers/ccp
A: driver_override=(null)
A: enable=1
L: firmware_node=../../../LNXSYSTM:00/LNXSYBUS:00/PNP0A08:02/device:8f/device:91
L: iommu=../../0000:20:00.2/iommu/ivhd2
L: iommu_group=../../../../kernel/iommu_groups/37
A: irq=256
A: link/l0s_aspm=0
A: link/l1_aspm=0
A: local_cpulist=0-23
A: local_cpus=00000000,00000000,00000000,00ffffff
A: max_link_speed=16.0 GT/s PCIe
A: max_link_width=16
A: modalias=pci:v00001022d00001486sv000017AAsd00001046bc10sc80i00
A: msi_bus=1
A: msi_irqs/257=msix
A: msi_irqs/258=msix
A: numa_node=-1
A: pools=poolinfo - 0.1ccp-1_q4            0    0   64  0ccp-1_q3            0    0   64  0ccp-1_q2            0    0   64  0
A: power/control=on
A: power/runtime_active_time=2767353428
A: power/runtime_status=active
A: power/runtime_suspended_time=0
A: power/wakeup=disabled
A: power/wakeup_abort_count=
A: power/wakeup_active=
A: power/wakeup_active_count=
A: power/wakeup_count=
A: power/wakeup_expire_count=
A: power/wakeup_last_time_ms=
A: power/wakeup_max_time_ms=
A: power/wakeup_total_time_ms=
A: power_state=D0
A: reset_method=flr
A: resource=0x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x00000000c3d00000 0x00000000c3dfffff 0x00000000000402000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x00000000c3e00000 0x00000000c3e01fff 0x00000000000402000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x0000000000000000
A: revision=0x00
A: subsystem_device=0x1046
A: subsystem_vendor=0x17aa
A: vendor=0x1022
"""
        )
        self.testbed.add_from_string(
            """P: /devices/pci0000:20/0000:20:08.1
E: DRIVER=pcieport
E: PCI_CLASS=60400
E: PCI_ID=1022:1484
E: PCI_SUBSYS_ID=1046:17AA
E: PCI_SLOT_NAME=0000:20:08.1
E: MODALIAS=pci:v00001022d00001484sv00001046sd000017AAbc06sc04i00
E: SUBSYSTEM=pci
E: ID_PCI_CLASS_FROM_DATABASE=Bridge
E: ID_PCI_SUBCLASS_FROM_DATABASE=PCI bridge
E: ID_PCI_INTERFACE_FROM_DATABASE=Normal decode
E: ID_VENDOR_FROM_DATABASE=Advanced Micro Devices, Inc. [AMD]
E: ID_MODEL_FROM_DATABASE=Starship/Matisse Internal PCIe GPP Bridge 0 to bus[E:B]
A: aer_dev_correctable=RxErr 0BadTLP 0BadDLLP 0Rollover 0Timeout 0NonFatalErr 0CorrIntErr 0HeaderOF 0TOTAL_ERR_COR 0
A: aer_dev_fatal=Undefined 0DLP 0SDES 0TLP 0FCP 0CmpltTO 0CmpltAbrt 0UnxCmplt 0RxOF 0MalfTLP 0ECRC 0UnsupReq 0ACSViol 0UncorrIntErr 0BlockedTLP 0AtomicOpBlocked 0TLPBlockedErr 0PoisonTLPBlocked 0TOTAL_ERR_FATAL 0
A: aer_dev_nonfatal=Undefined 0DLP 0SDES 0TLP 0FCP 0CmpltTO 0CmpltAbrt 0UnxCmplt 0RxOF 0MalfTLP 0ECRC 0UnsupReq 0ACSViol 0UncorrIntErr 0BlockedTLP 0AtomicOpBlocked 0TLPBlockedErr 0PoisonTLPBlocked 0TOTAL_ERR_NONFATAL 0
A: aer_rootport_total_err_cor=0
A: aer_rootport_total_err_fatal=0
A: aer_rootport_total_err_nonfatal=0
A: ari_enabled=0
A: broken_parity_status=0
A: class=0x060400
H: config=22108414070410000000040610000100000000000000000020232300F1010000C0C3E0C3F1FF01000000000000000000000000005000000000000000FF011200
A: consistent_dma_mask_bits=32
A: current_link_speed=16.0 GT/s PCIe
A: current_link_width=16
A: d3cold_allowed=1
A: device=0x1484
A: dma_mask_bits=32
L: driver=../../../bus/pci/drivers/pcieport
A: driver_override=(null)
A: enable=2
L: firmware_node=../../LNXSYSTM:00/LNXSYBUS:00/PNP0A08:02/device:8f
L: iommu=../0000:20:00.2/iommu/ivhd2
L: iommu_group=../../../kernel/iommu_groups/33
A: irq=43
A: local_cpulist=0-23
A: local_cpus=00000000,00000000,00000000,00ffffff
A: max_link_speed=16.0 GT/s PCIe
A: max_link_width=16
A: modalias=pci:v00001022d00001484sv00001046sd000017AAbc06sc04i00
A: msi_bus=1
A: msi_irqs/43=msi
A: numa_node=-1
A: power/autosuspend_delay_ms=100
A: power/control=auto
A: power/runtime_active_time=2767353433
A: power/runtime_status=active
A: power/runtime_suspended_time=0
A: power/wakeup=enabled
A: power/wakeup_abort_count=0
A: power/wakeup_active=0
A: power/wakeup_active_count=0
A: power/wakeup_count=0
A: power/wakeup_expire_count=0
A: power/wakeup_last_time_ms=0
A: power/wakeup_max_time_ms=0
A: power/wakeup_total_time_ms=0
A: power_state=D0
A: reset_method=pm
A: resource=0x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x00000000c3c00000 0x00000000c3efffff 0x00000000000002000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x0000000000000000
A: revision=0x00
A: secondary_bus_number=35
A: subordinate_bus_number=35
A: subsystem_device=0x17aa
A: subsystem_vendor=0x1046
A: vendor=0x1022
"""
        )

    def test_pci_psp_device(self):
        """Verify the PCI PSP device is detected correctly"""
        self.start_daemon()
        devices = Fwupd.Client().get_devices()

        count = 0
        for dev in devices:
            if dev.get_plugin() != "pci_psp":
                continue
            self.assertEqual(dev.get_name(), "Secure Processor")
            self.assertEqual(dev.get_version(), None)
            self.assertEqual(dev.get_version_bootloader(), None)
            count += 1
        self.assertEqual(count, 1)

    def test_pci_psp_hsi(self):
        """Verify the PCI PSP HSI attributes are detected correctly"""
        self.start_daemon()
        attrs = Fwupd.Client().get_host_security_attrs()
        for attr in attrs:
            if attr.get_plugin() != "pci_psp":
                continue
            if attr.get_appstream_id() == "org.fwupd.hsi.PlatformFused":
                self.assertEqual(attr.get_name(), "Fused platform")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.UNKNOWN)
                self.assertEqual(attr.get_level(), 1)
            elif attr.get_appstream_id() == "org.fwupd.hsi.EncryptedRam":
                self.assertEqual(attr.get_name(), "Encrypted RAM")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.UNKNOWN)
                self.assertEqual(attr.get_level(), 4)
            elif attr.get_appstream_id() == "org.fwupd.hsi.Amd.RollbackProtection":
                self.assertEqual(attr.get_name(), "Processor rollback protection")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.UNKNOWN)
                self.assertEqual(attr.get_level(), 4)
            elif attr.get_appstream_id() == "org.fwupd.hsi.Amd.SpiReplayProtection":
                self.assertEqual(attr.get_name(), "SPI replay protection")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.UNKNOWN)
                self.assertEqual(attr.get_level(), 3)
            elif attr.get_appstream_id() == "org.fwupd.hsi.PlatformDebugLocked":
                self.assertEqual(attr.get_name(), "Platform debugging")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.UNKNOWN)
                self.assertEqual(attr.get_level(), 2)
            elif attr.get_appstream_id() == "org.fwupd.hsi.Amd.SpiWriteProtection":
                self.assertEqual(attr.get_name(), "SPI write protection")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.UNKNOWN)
                self.assertEqual(attr.get_level(), 2)


class PciPspWithHsi(FwupdTest):
    """A system that can export both HSI attributes and versions"""

    def setUp(self):
        super().setUp()
        self.testbed.add_from_string(
            """P: /devices/pci0000:00/0000:00:08.1/0000:c1:00.2
E: DRIVER=ccp
E: ID_MODEL_FROM_DATABASE=Family 19h (Model 74h) CCP/PSP 3.0 Device
E: ID_PATH=pci-0000:c1:00.2
E: ID_PATH_TAG=pci-0000_c1_00_2
E: ID_PCI_CLASS_FROM_DATABASE=Encryption controller
E: ID_PCI_SUBCLASS_FROM_DATABASE=Encryption controller
E: ID_VENDOR_FROM_DATABASE=Advanced Micro Devices, Inc. [AMD]
E: MODALIAS=pci:v00001022d000015C7sv0000F111sd00000006bc10sc80i00
E: PCI_CLASS=108000
E: PCI_ID=1022:15C7
E: PCI_SLOT_NAME=0000:c1:00.2
E: PCI_SUBSYS_ID=F111:0006
E: SUBSYSTEM=pci
A: anti_rollback_status=0
A: ari_enabled=0
A: bootloader_version=00.2d.00.78
A: broken_parity_status=0
A: class=0x108000
H: config=2210C715070410000000801010008000000000000000000000004090000000000000000000C05C900000000011F10600000000004800000000000000FF030000
A: consistent_dma_mask_bits=48
A: current_link_speed=16.0 GT/s PCIe
A: current_link_width=16
A: d3cold_allowed=1
A: debug_lock_on=1
A: device=0x15c7
A: dma_mask_bits=48
L: driver=../../../../bus/pci/drivers/ccp
A: driver_override=(null)
A: enable=1
L: firmware_node=../../../LNXSYSTM:00/LNXSYBUS:00/PNP0A08:00/device:16/device:18
A: fused_part=1
A: hsp_tpm_available=0
L: iommu=../../0000:00:00.2/iommu/ivhd0
L: iommu_group=../../../../kernel/iommu_groups/16
A: irq=109
A: link/l0s_aspm=0
A: link/l1_aspm=0
A: local_cpulist=0-11
A: local_cpus=0fff
A: max_link_speed=16.0 GT/s PCIe
A: max_link_width=16
A: modalias=pci:v00001022d000015C7sv0000F111sd00000006bc10sc80i00
A: msi_bus=1
A: msi_irqs/110=msix
A: msi_irqs/111=msix
A: numa_node=-1
A: power/control=on
A: power/runtime_active_time=17568317
A: power/runtime_status=active
A: power/runtime_suspended_time=0
A: power/wakeup=disabled
A: power/wakeup_abort_count=
A: power/wakeup_active=
A: power/wakeup_active_count=
A: power/wakeup_count=
A: power/wakeup_expire_count=
A: power/wakeup_last_time_ms=
A: power/wakeup_max_time_ms=
A: power/wakeup_total_time_ms=
A: power_state=D0
A: resource=0x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000090400000 0x00000000904fffff 0x00000000000402000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x00000000905cc000 0x00000000905cdfff 0x00000000000402000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x0000000000000000
A: revision=0x00
A: rom_armor_enforced=1
A: rpmc_production_enabled=0
A: rpmc_spirom_available=1
A: subsystem_device=0x0006
A: subsystem_vendor=0xf111
A: tee_version=00.2d.00.78
A: tsme_status=0
A: vendor=0x1022
"""
        )
        self.testbed.add_from_string(
            """P: /devices/pci0000:00/0000:00:08.1
E: DRIVER=pcieport
E: ID_PATH=pci-0000:00:08.1
E: ID_PATH_TAG=pci-0000_00_08_1
E: ID_PCI_CLASS_FROM_DATABASE=Bridge
E: ID_PCI_INTERFACE_FROM_DATABASE=Normal decode
E: ID_PCI_SUBCLASS_FROM_DATABASE=PCI bridge
E: ID_VENDOR_FROM_DATABASE=Advanced Micro Devices, Inc. [AMD]
E: MODALIAS=pci:v00001022d000014EBsv00000006sd0000F111bc06sc04i00
E: PCI_CLASS=60400
E: PCI_ID=1022:14EB
E: PCI_SLOT_NAME=0000:00:08.1
E: PCI_SUBSYS_ID=0006:F111
E: SUBSYSTEM=pci
A: ari_enabled=0
A: broken_parity_status=0
A: class=0x060400
H: config=2210EB14070410000000040610008100000000000000000000C1C1001111000000905090010071107800000078000000000000005000000000000000FF010200
A: consistent_dma_mask_bits=32
A: current_link_speed=16.0 GT/s PCIe
A: current_link_width=16
A: d3cold_allowed=1
A: device=0x14eb
A: dma_mask_bits=32
L: driver=../../../bus/pci/drivers/pcieport
A: driver_override=(null)
A: enable=2
L: firmware_node=../../LNXSYSTM:00/LNXSYBUS:00/PNP0A08:00/device:16
L: iommu=../0000:00:00.2/iommu/ivhd0
L: iommu_group=../../../kernel/iommu_groups/7
A: irq=42
A: local_cpulist=0-11
A: local_cpus=0fff
A: max_link_speed=16.0 GT/s PCIe
A: max_link_width=16
A: modalias=pci:v00001022d000014EBsv00000006sd0000F111bc06sc04i00
A: msi_bus=1
A: msi_irqs/42=msi
A: numa_node=-1
A: power/autosuspend_delay_ms=100
A: power/control=auto
A: power/runtime_active_time=17568337
A: power/runtime_status=active
A: power/runtime_suspended_time=0
A: power/wakeup=disabled
A: power/wakeup_abort_count=
A: power/wakeup_active=
A: power/wakeup_active_count=
A: power/wakeup_count=
A: power/wakeup_expire_count=
A: power/wakeup_last_time_ms=
A: power/wakeup_max_time_ms=
A: power/wakeup_total_time_ms=
A: power_state=D0
A: reset_method=pm
A: resource=0x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000000000 0x0000000000000000 0x00000000000000000x0000000000001000 0x0000000000001fff 0x00000000000001010x0000000090000000 0x00000000905fffff 0x00000000000002000x0000007800000000 0x00000078107fffff 0x00000000001022010x0000000000000000 0x0000000000000000 0x0000000000000000
A: revision=0x00
A: secondary_bus_number=193
A: subordinate_bus_number=193
A: subsystem_device=0xf111
A: subsystem_vendor=0x0006
A: vendor=0x1022
"""
        )

    def test_pci_psp_device(self):
        """Verify the PCI PSP device is detected correctly"""
        self.start_daemon()
        devices = Fwupd.Client().get_devices()

        count = 0
        for dev in devices:
            if dev.get_plugin() != "pci_psp":
                continue
            self.assertEqual(dev.get_name(), "Secure Processor")
            self.assertEqual(dev.get_version(), "00.2d.00.78")
            self.assertEqual(dev.get_version_bootloader(), "00.2d.00.78")
            count += 1
        self.assertEqual(count, 1)

    def test_pci_psp_hsi(self):
        """Verify the PCI PSP HSI attributes are detected correctly"""
        self.start_daemon()
        attrs = Fwupd.Client().get_host_security_attrs()
        for attr in attrs:
            if attr.get_plugin() != "pci_psp":
                continue
            if attr.get_appstream_id() == "org.fwupd.hsi.PlatformFused":
                self.assertEqual(attr.get_name(), "Fused platform")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.LOCKED)
                self.assertEqual(attr.get_level(), 1)
            elif attr.get_appstream_id() == "org.fwupd.hsi.EncryptedRam":
                self.assertEqual(attr.get_name(), "Encrypted RAM")
                self.assertEqual(
                    attr.get_result(), Fwupd.SecurityAttrResult.NOT_SUPPORTED
                )
                self.assertEqual(attr.get_level(), 4)
            elif attr.get_appstream_id() == "org.fwupd.hsi.Amd.RollbackProtection":
                self.assertEqual(attr.get_name(), "Processor rollback protection")
                self.assertEqual(
                    attr.get_result(), Fwupd.SecurityAttrResult.NOT_ENABLED
                )
                self.assertEqual(attr.get_level(), 4)
            elif attr.get_appstream_id() == "org.fwupd.hsi.Amd.SpiReplayProtection":
                self.assertEqual(attr.get_name(), "SPI replay protection")
                self.assertEqual(
                    attr.get_result(), Fwupd.SecurityAttrResult.NOT_ENABLED
                )
                self.assertEqual(attr.get_level(), 3)
            elif attr.get_appstream_id() == "org.fwupd.hsi.PlatformDebugLocked":
                self.assertEqual(attr.get_name(), "Platform debugging")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.LOCKED)
                self.assertEqual(attr.get_level(), 2)
            elif attr.get_appstream_id() == "org.fwupd.hsi.Amd.SpiWriteProtection":
                self.assertEqual(attr.get_name(), "SPI write protection")
                self.assertEqual(attr.get_result(), Fwupd.SecurityAttrResult.ENABLED)
                self.assertEqual(attr.get_level(), 2)


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
