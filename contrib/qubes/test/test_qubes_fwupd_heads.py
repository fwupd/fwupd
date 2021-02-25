#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import io
import os
import platform
import shutil
import imp
import src.qubes_fwupd_heads as qf_heads
import sys
import unittest

from test.fwupd_logs import HEADS_XML

CUSTOM_METADATA = "https://fwupd.org/downloads/firmware-3c81bfdc9db5c8a42c09d38091944bc1a05b27b0.xml.gz"
QUBES_FWUPDMGR_REPO = "./src/qubes_fwupdmgr.py"
QUBES_FWUPDMGR_BINDIR = "/usr/sbin/qubes-fwupdmgr"


class TestQubesFwupdHeads(unittest.TestCase):
    def setUp(self):
        if os.path.exists(QUBES_FWUPDMGR_REPO):
            self.qfwupd = imp.load_source(
                "qubes_fwupdmgr",
                QUBES_FWUPDMGR_REPO
            )
        elif os.path.exists(QUBES_FWUPDMGR_BINDIR):
            self.qfwupd = imp.load_source(
                "qubes_fwupdmgr",
                QUBES_FWUPDMGR_BINDIR
            )
        self.q = qf_heads.FwupdHeads()
        self.maxDiff = 2000
        self.captured_output = io.StringIO()
        sys.stdout = self.captured_output

    @unittest.skipUnless('qubes' in platform.release(), "Requires Qubes OS")
    def test_get_hwids(self):
        self.q._check_fwupdtool_version()
        self.q._get_hwids()
        self.assertNotEqual(self.q.dom0_hwids_info, "")

    def test_gather_firmware_version_empty(self):
        self.q.dom0_hwids_info = ""
        return_code = self.q._gather_firmware_version()
        self.assertEqual(return_code, 2)

    def test_gather_firmware_version(self):
        self.q.dom0_hwids_info = "BiosVersion: CBET4000 0.2.2 heads"
        self.q._gather_firmware_version()
        self.assertEqual(self.q.heads_version, "0.2.2")

    @unittest.skipUnless('qubes' in platform.release(), "Requires Qubes OS")
    def test_parse_metadata(self):
        qmgr = self.qfwupd.QubesFwupdmgr()
        qmgr.metadata_file = CUSTOM_METADATA.replace(
            "https://fwupd.org/downloads",
            self.qfwupd.FWUPD_DOM0_METADATA_DIR
        )
        qmgr._download_metadata(metadata_url=CUSTOM_METADATA)
        self.q._parse_metadata(qmgr.metadata_file)
        self.assertTrue(self.q.metadata_info)

    def test_check_heads_updates_default_heads(self):
        self.q.metadata_info = HEADS_XML
        self.q.heads_version = "heads"
        return_code = self.q._parse_heads_updates("x230")
        self.assertEqual(return_code, 0)
        self.assertEqual(
            self.q.heads_update_url,
            "https://fwupd.org/downloads/e747a435bf24fd6081b77b6704b39cec5fa2dcf62e0ca6b86d8a6460121a1d07-heads_coreboot_x230-v0_2_3.cab"
        )
        self.assertEqual(
            self.q.heads_update_sha,
            "1a54e69ca2b58d1218035115d481480eaf4c66e4"
        )
        self.assertEqual(
            self.q.heads_update_version,
            "0.2.3"
        )

    def test_check_heads_updates_no_updates(self):
        self.q.metadata_info = HEADS_XML
        self.q.heads_version = "0.2.3"
        return_code = self.q._parse_heads_updates("x230")
        self.assertEqual(return_code, 2)

    def test_check_heads_updates_lower_version(self):
        self.q.metadata_info = HEADS_XML
        self.q.heads_version = "0.2.2"
        return_code = self.q._parse_heads_updates("x230")
        self.assertEqual(return_code, 0)
        self.assertEqual(
            self.q.heads_update_url,
            "https://fwupd.org/downloads/e747a435bf24fd6081b77b6704b39cec5fa2dcf62e0ca6b86d8a6460121a1d07-heads_coreboot_x230-v0_2_3.cab"
        )
        self.assertEqual(
            self.q.heads_update_sha,
            "1a54e69ca2b58d1218035115d481480eaf4c66e4"
        )
        self.assertEqual(
            self.q.heads_update_version,
            "0.2.3"
        )

    @unittest.skipUnless('qubes' in platform.release(), "Requires Qubes OS")
    def test_copy_heads_firmware(self):
        qmgr = self.qfwupd.QubesFwupdmgr()
        self.q.heads_update_url = "https://fwupd.org/downloads/e747a435bf24fd6081b77b6704b39cec5fa2dcf62e0ca6b86d8a6460121a1d07-heads_coreboot_x230-v0_2_3.cab"
        self.q.heads_update_sha = "1a54e69ca2b58d1218035115d481480eaf4c66e4"
        self.q.heads_update_version = "0.2.3"
        qmgr._download_firmware_updates(
            self.q.heads_update_url,
            self.q.heads_update_sha
        )
        heads_boot_path = os.path.join(
            qf_heads.HEADS_UPDATES_DIR,
            self.q.heads_update_version
        )
        if os.path.exists(heads_boot_path):
            shutil.rmtree(heads_boot_path)
        ret_code = self.q._copy_heads_firmware(qmgr.arch_path)
        self.assertNotEqual(ret_code, self.qfwupd.EXIT_CODES["NO_UPDATES"])
        firmware_path = os.path.join(heads_boot_path, "firmware.rom")
        self.assertTrue(os.path.exists(firmware_path))


if __name__ == '__main__':
    unittest.main()
