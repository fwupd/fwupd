#!/usr/bin/env python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright 2021 Norbert Kamiński <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import json
import unittest
import os
import subprocess
import sys
import importlib.util
import io
import platform
import tempfile
from .fwupd_logs import UPDATE_INFO, GET_DEVICES, DMI_DECODE
from .fwupd_logs import GET_DEVICES_NO_VERSION
from unittest.mock import patch


_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
QUBES_FWUPDMGR_REPO = os.path.join(_THIS_DIR, "..", "src", "qubes_fwupdmgr.py")
QUBES_FWUPDMGR_BINDIR = "/usr/sbin/qubes-fwupdmgr"

if os.path.exists(QUBES_FWUPDMGR_REPO):
    _qfwupd_path = QUBES_FWUPDMGR_REPO
elif os.path.exists(QUBES_FWUPDMGR_BINDIR):
    _qfwupd_path = QUBES_FWUPDMGR_BINDIR
else:
    _qfwupd_path = None

qfwupd = None
if _qfwupd_path is not None:
    _spec = importlib.util.spec_from_file_location("qubes_fwupdmgr", _qfwupd_path)
    qfwupd = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(qfwupd)

FWUPD_DOM0_DIR = "/var/cache/fwupd/qubes"
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOM0_UNTRUSTED_DIR = os.path.join(FWUPD_DOM0_UPDATES_DIR, "untrusted")
FWUPD_DOM0_METADATA_DIR = os.path.join(FWUPD_DOM0_DIR, "metadata")
FWUPD_DOM0_METADATA_FILE = os.path.join(FWUPD_DOM0_METADATA_DIR, "firmware.xml.xz")
FWUPD_DOM0_METADATA_FILE_JCAT = os.path.join(FWUPD_DOM0_METADATA_DIR, "firmware.xml.xz")
REQUIRED_DEV = "Requires device not connected"
XL_LIST_LOG = "Name                                        ID   Mem VCPUs	State	Time(s)"
FWUPDMGR = "/bin/fwupdmgr"
BIOS_UPDATE_FLAG = os.path.join(FWUPD_DOM0_DIR, "bios_update")
LVFS_TESTING_DOM0_FLAG = os.path.join(FWUPD_DOM0_DIR, "lvfs_testing")
CUSTOM_METADATA = "https://fwupd.org/downloads/firmware-3c81bfdc9db5c8a42c09d38091944bc1a05b27b0.xml.gz"

# the fixtures under ./test/logs take the size and hashes of this stub
STUB_FIRMWARE = b"qubes-fwupd test firmware\n"


def running_in_dom0():
    return "qubes" in platform.release() and os.path.exists("/etc/qubes-release")


def device_connected_dom0():
    """Checks if the testing device is connected in dom0"""
    if not running_in_dom0():
        return False
    q = qfwupd.QubesFwupdmgr()
    q._get_dom0_devices()
    return "ColorHug2" in q.dom0_devices_info


def check_whonix_updatevm():
    """Checks if the sys-whonix is running"""
    if not running_in_dom0():
        return False
    q = qfwupd.QubesFwupdmgr()
    q.updatevm = "sys-whonix"
    return q._check_updatevm()


class TestQubesFwupdmgr(unittest.TestCase):
    def setUp(self):
        self.q = qfwupd.QubesFwupdmgr()
        self.maxDiff = 2000
        self.captured_output = io.StringIO()
        self.orig_stdout = sys.stdout
        sys.stdout = self.captured_output

    def tearDown(self):
        sys.stdout = self.orig_stdout

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_download_metadata(self):
        self.q.metadata_file = FWUPD_DOM0_METADATA_FILE
        self.q._download_metadata(metadata_url=qfwupd.METADATA_URL)
        self.assertTrue(
            os.path.exists(FWUPD_DOM0_METADATA_FILE),
            msg="Metadata update file does not exist",
        )
        self.assertTrue(
            os.path.exists(FWUPD_DOM0_METADATA_FILE_JCAT),
            msg="Metadata signature does not exist",
        )

    @unittest.skipUnless(check_whonix_updatevm(), "Requires sys-whonix")
    def test_download_metadata_whonix(self):
        self.q.metadata_file = FWUPD_DOM0_METADATA_FILE
        self.q._download_metadata(whonix=True)
        self.assertTrue(
            os.path.exists(FWUPD_DOM0_METADATA_FILE),
            msg="Metadata update file does not exist",
        )
        self.assertTrue(
            os.path.exists(FWUPD_DOM0_METADATA_FILE_JCAT),
            msg="Metadata signature does not exist",
        )

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_download_custom_metadata(self):
        self.q.metadata_file = CUSTOM_METADATA.replace(
            "https://fwupd.org/downloads", FWUPD_DOM0_METADATA_DIR
        )
        self.q.metadata_file_jcat = self.q.metadata_file + ".jcat"
        self.q._download_metadata(metadata_url=CUSTOM_METADATA)
        self.assertTrue(
            os.path.exists(self.q.metadata_file),
            msg="Metadata update file does not exist",
        )
        self.assertTrue(
            os.path.exists(self.q.metadata_file_jcat),
            msg="Metadata signature does not exist",
        )

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_refresh_metadata_dom0(self):
        self.q.refresh_metadata(metadata_url=qfwupd.METADATA_URL)
        self.assertEqual(
            self.captured_output.getvalue().strip(),
            "Successfully refreshed metadata manually",
            msg="Metadata refresh failed.",
        )

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    @unittest.expectedFailure  # fwupd refuses metadata downgrade
    def test_refresh_metadata_dom0_custom(self):
        self.q.refresh_metadata(metadata_url=CUSTOM_METADATA)
        self.assertEqual(
            self.captured_output.getvalue().strip(),
            "Successfully refreshed metadata manually",
            msg="Metadata refresh failed.",
        )

    @unittest.skipUnless(check_whonix_updatevm(), "Requires sys-whonix")
    def test_refresh_metadata_whonix(self):
        self.q.refresh_metadata(whonix=True, metadata_url=qfwupd.METADATA_URL)
        self.assertEqual(
            self.captured_output.getvalue().strip(),
            "Successfully refreshed metadata manually",
            msg="Metadata refresh failed.",
        )

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_get_dom0_updates(self):
        self.q._get_dom0_updates()
        self.assertIn(
            "Devices", self.q.dom0_updates_info, msg="Getting available updates failed"
        )

    def test_parse_updates_info(self):
        self.q._parse_dom0_updates_info(UPDATE_INFO)
        self.assertEqual(
            self.q.dom0_updates_list[0]["Name"], "ColorHug2", msg="Wrong device name"
        )
        self.assertEqual(
            self.q.dom0_updates_list[0]["Version"], "2.0.6", msg="Wrong update version"
        )
        self.assertEqual(
            self.q.dom0_updates_list[0]["Releases"][0]["Url"],
            "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
            msg="Wrong update URL",
        )
        self.assertEqual(
            self.q.dom0_updates_list[0]["Releases"][0]["Checksum"],
            "32c4a2c9be787cdf1d757c489d6455bd7bb14053425180b6d331c37e1ccc1cda",
            msg="Wrong checksum",
        )

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_download_firmware_updates(self):
        self.q._download_firmware_updates(
            "https://fwupd.org/downloads/e5ad222bdbd3d3d48d8613e67c7e0a0e194f"
            "8cd828e33c554d9f05d933e482c7-hughski-colorhug2-2.0.7.cab",
            "e5ad222bdbd3d3d48d8613e67c7e0a0e194f8cd828e33c554d9f05d933e482c7",
        )
        update_path = os.path.join(
            FWUPD_DOM0_UPDATES_DIR,
            "e5ad222bdbd3d3d48d8613e67c7e0a0e194f8cd828e33c554d9f05d933e482c7"
            "-hughski-colorhug2-2.0.7.cab",
        )
        self.assertTrue(os.path.exists(update_path))

    @unittest.skipUnless(check_whonix_updatevm(), "Requires sys-whonix")
    def test_download_firmware_updates_whonix(self):
        self.q._download_firmware_updates(
            "https://fwupd.org/downloads/e5ad222bdbd3d3d48d8613e67c7e0a0e194f"
            "8cd828e33c554d9f05d933e482c7-hughski-colorhug2-2.0.7.cab",
            "e5ad222bdbd3d3d48d8613e67c7e0a0e194f8cd828e33c554d9f05d933e482c7",
            whonix=True,
        )
        update_path = os.path.join(
            FWUPD_DOM0_UPDATES_DIR,
            "e5ad222bdbd3d3d48d8613e67c7e0a0e194f8cd828e33c554d9f05d933e482c7"
            "-hughski-colorhug2-2.0.7.cab",
        )
        self.assertTrue(os.path.exists(update_path))

    def test_user_input_empty_dict(self):
        self.assertEqual(self.q._user_input([]), -2)

    def test_user_input_n(self):
        user_input = ["sth", "n"]
        with patch("builtins.input", side_effect=user_input):
            self.q._parse_dom0_updates_info(UPDATE_INFO)
            choice = self.q._user_input(self.q.dom0_updates_list)
        self.assertEqual(choice, -2)
        user_input = ["sth", "N"]
        with patch("builtins.input", side_effect=user_input):
            self.q._parse_dom0_updates_info(UPDATE_INFO)
            choice = self.q._user_input(self.q.dom0_updates_list)
        self.assertEqual(choice, -2)

    def test_user_input_choice(self):
        user_input = ["6", "1"]
        with patch("builtins.input", side_effect=user_input):
            self.q._parse_dom0_updates_info(UPDATE_INFO)
            choice = self.q._user_input(self.q.dom0_updates_list)
        self.assertEqual(choice, 0)

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_compare_versions(self):
        ascending = [
            ("09", "010"),
            ("0.4", "0.10"),
            ("0.000.00004", "0.000.000010"),
            ("9.0", "10.0"),
            ("A", "B"),
            (" 9", " 10"),
            ("0.20.30.40", "1.2.3.4"),
            ("0.1001", "1.0"),
            ("0xABC", "0xABD"),
            ("0Xabc", "0Xabd"),
            ("1.2.3", "1.2.3.0"),
            ("P0.1", "P1.0"),
        ]
        for lower, higher in ascending:
            with self.subTest(lower=lower, higher=higher):
                self.assertEqual(self.q._compare_versions(lower, higher), -1)
                self.assertEqual(self.q._compare_versions(higher, lower), 1)
                self.assertEqual(self.q._compare_versions(lower, lower), 0)
                self.assertEqual(self.q._compare_versions(higher, higher), 0)

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_compare_versions_numeric_chunks(self):
        self.assertEqual(self.q._compare_versions("10..0", "9..0"), 1)
        self.assertEqual(self.q._compare_versions("10a", "9a"), 1)
        self.assertEqual(self.q._compare_versions("10_0", "9_0"), 1)

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_compare_versions_version_format(self):
        """The device's version format changes the ordering"""
        self.assertEqual(self.q._compare_versions("10a", "9a"), 1)
        self.assertEqual(self.q._compare_versions("10a", "9a", "plain"), -1)
        self.assertEqual(self.q._compare_versions("10a", "9a", "unknown"), 1)
        self.assertEqual(self.q._compare_versions("2.0.6", "2.0.7", "triplet"), -1)
        self.assertEqual(self.q._compare_versions("0x1020304", "0x1020305", "hex"), -1)

    def test_compare_versions_invalid(self):
        """Version specifiers that cannot be compared are rejected"""
        for invalid in ("", "      \r\n", "1.2\n3", "-1.0", None, 1.0):
            with self.subTest(version=invalid):
                with self.assertRaises(ValueError):
                    self.q._compare_versions(invalid, "1.0")
                with self.assertRaises(ValueError):
                    self.q._compare_versions("1.0", invalid)

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_parse_parameters(self):
        self.q._parse_dom0_updates_info(UPDATE_INFO)
        self.q._parse_parameters(self.q.dom0_updates_list, 0)
        self.assertEqual(
            self.q.url,
            "https://fwupd.org/downloads/0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab",
        )
        self.assertEqual(
            self.q.sha,
            "32c4a2c9be787cdf1d757c489d6455bd7bb14053425180b6d331c37e1ccc1cda",
        )
        self.assertEqual(self.q.version, "2.0.7")

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_clean_cache_dom0(self):
        self.q.clean_cache()
        self.assertFalse(os.path.exists(FWUPD_DOM0_METADATA_DIR))
        self.assertFalse(os.path.exists(FWUPD_DOM0_UNTRUSTED_DIR))

    def test_output_crawler(self):
        crawler_output = io.StringIO()
        sys.stdout = crawler_output
        self.q._output_crawler(json.loads(UPDATE_INFO), 0)
        with open("test/logs/get_devices.log") as get_devices:
            self.assertEqual(
                get_devices.read(), crawler_output.getvalue().strip() + "\n"
            )
        sys.stdout = self.captured_output

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_get_dom0_devices(self):
        self.q._get_dom0_devices()
        self.assertIsNotNone(self.q.dom0_devices_info)

    @unittest.skipUnless(running_in_dom0(), "Requires Qubes OS dom0")
    def test_get_devices_qubes_dom0(self):
        get_devices_output = io.StringIO()
        sys.stdout = get_devices_output
        self.q.get_devices_qubes()
        self.assertNotEqual(get_devices_output.getvalue().strip(), "")
        sys.stdout = self.captured_output

    @unittest.skipUnless(device_connected_dom0(), REQUIRED_DEV)
    def test_get_updates_qubes_dom0(self):
        get_updates_output = io.StringIO()
        sys.stdout = get_updates_output
        self.q.get_updates_qubes()
        self.assertNotEqual(get_updates_output.getvalue().strip(), "")
        sys.stdout = self.captured_output

    def test_help(self):
        help_output = io.StringIO()
        sys.stdout = help_output
        self.q.help()
        with open("test/logs/help.log") as help_log:

            def _strip_lines(text):
                return (
                    "\n".join(line.rstrip() for line in text.strip().splitlines())
                    + "\n"
                )

            self.assertEqual(
                _strip_lines(help_log.read()), _strip_lines(help_output.getvalue())
            )
        sys.stdout = self.captured_output

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    @patch(
        "test.test_qubes_fwupdmgr.qfwupd.QubesFwupdmgr._read_dmi",
        return_value=DMI_DECODE,
    )
    def test_verify_dmi(self, output):
        self.q.dmi_version = "P.1.0"
        with tempfile.TemporaryDirectory() as tmpdir:
            arch_name = tmpdir + "/firmware.cab"
            firmware_bin = tmpdir + "/firmware.bin"
            with open(firmware_bin, "wb") as firmware:
                firmware.write(STUB_FIRMWARE)
            subprocess.check_call(
                [
                    "fwupdtool",
                    "build-cabinet",
                    arch_name,
                    firmware_bin,
                    "firmware.metainfo.xml",
                ],
                cwd="test/logs",
            )
            self.q._verify_dmi(arch_name, "P1.1")

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    @patch(
        "test.test_qubes_fwupdmgr.qfwupd.QubesFwupdmgr._read_dmi",
        return_value=DMI_DECODE,
    )
    def test_verify_dmi_wrong_vendor(self, output):
        with self.assertRaises(ValueError) as wrong_vendor:
            self.q.dmi_version = "P.1.0"
            with tempfile.TemporaryDirectory() as tmpdir:
                arch_name = tmpdir + "/firmware.cab"
                firmware_bin = tmpdir + "/firmware.bin"
                with open(firmware_bin, "wb") as firmware:
                    firmware.write(STUB_FIRMWARE)
                subprocess.check_call(
                    [
                        "fwupdtool",
                        "build-cabinet",
                        arch_name,
                        firmware_bin,
                        "firmware.metainfo.xml",
                    ],
                    cwd="test/logs/metainfo_name",
                )
                self.q._verify_dmi(arch_name, "P1.1")
        self.assertIn("Wrong firmware provider.", str(wrong_vendor.exception))

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    @patch(
        "test.test_qubes_fwupdmgr.qfwupd.QubesFwupdmgr._read_dmi",
        return_value=DMI_DECODE,
    )
    def test_verify_dmi_version(self, output):
        self.q.dmi_version = "P1.0"
        with self.assertRaises(ValueError) as downgrade:
            with tempfile.TemporaryDirectory() as tmpdir:
                arch_name = tmpdir + "/firmware.cab"
                firmware_bin = tmpdir + "/firmware.bin"
                with open(firmware_bin, "wb") as firmware:
                    firmware.write(STUB_FIRMWARE)
                subprocess.check_call(
                    [
                        "fwupdtool",
                        "build-cabinet",
                        arch_name,
                        firmware_bin,
                        "firmware.metainfo.xml",
                    ],
                    cwd="test/logs/metainfo_version",
                )
                self.q._verify_dmi(arch_name, "P0.1")
        self.assertIn("P0.1 < P1.0 Downgrade not allowed", str(downgrade.exception))

    @unittest.skipUnless(device_connected_dom0(), REQUIRED_DEV)
    def test_downgrade_firmware_dom0(self):
        old_version = None
        self.q._get_dom0_devices()
        downgrades = self.q._parse_downgrades(self.q.dom0_devices_info)
        for number, device in enumerate(downgrades):
            if "Name" not in device:
                continue
            if device["Name"] == "ColorHug2":
                old_version = device["Version"]
                break
        if old_version is None:
            self.fail("Test device not found")
        user_input = [str(number + 1), "1"]
        with patch("builtins.input", side_effect=user_input):
            self.q.downgrade_firmware()
        self.q._get_dom0_devices()
        downgrades = self.q._parse_downgrades(self.q.dom0_devices_info)
        new_version = downgrades[number]["Version"]
        self.assertEqual(self.q._compare_versions(old_version, new_version), 1)

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_parse_downgrades(self):
        downgrades = self.q._parse_downgrades(GET_DEVICES)
        self.assertEqual(downgrades[0]["Name"], "ColorHug2")
        self.assertEqual(downgrades[0]["Version"], "2.0.6")
        self.assertEqual(downgrades[0]["Releases"][0]["Version"], "2.0.5")
        self.assertEqual(
            downgrades[0]["Releases"][0]["Url"],
            "https://fwupd.org/downloads/f7dd4ab29fa610438571b8b62b26b0b0e57bb35b-hughski-colorhug2-2.0.5.cab",
        )
        self.assertEqual(
            downgrades[0]["Releases"][0]["Checksum"],
            "8cd379eb2e1467e4fda92c20650306dc7e598b1d421841bbe19d9ed6ea01e3ee",
        )

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_parse_downgrades_no_version(self):
        downgrades = self.q._parse_downgrades(GET_DEVICES_NO_VERSION)
        self.assertEqual(downgrades[0]["Name"], "ColorHug2")
        self.assertEqual(downgrades[0]["Version"], "2.0.6")
        self.assertEqual(downgrades[0]["Releases"][0]["Version"], "2.0.5")
        self.assertEqual(
            downgrades[0]["Releases"][0]["Url"],
            "https://fwupd.org/downloads/f7dd4ab29fa610438571b8b62b26b0b0e57bb35b-hughski-colorhug2-2.0.5.cab",
        )
        self.assertEqual(
            downgrades[0]["Releases"][0]["Checksum"],
            "4ee9dfa38df3b810f739d8a19d13da1b3175fb87",
        )

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_user_input_downgrade_dom0(self):
        user_input = ["1", "6", "sth", "2.2.1", "", " ", "\0", "2"]
        with patch("builtins.input", side_effect=user_input):
            downgrade_list = self.q._parse_downgrades(GET_DEVICES)
            downgrade_dict = downgrade_list
            device_choice, downgrade_choice = self.q._user_input(
                downgrade_dict, downgrade=True
            )
        self.assertEqual(device_choice, 0)
        self.assertEqual(downgrade_choice, 1)

    @unittest.skipUnless(running_in_dom0(), "Requires fwupdtool (dom0)")
    def test_user_input_downgrade_N(self):
        user_input = ["N"]
        with patch("builtins.input", side_effect=user_input):
            downgrade_list = self.q._parse_downgrades(GET_DEVICES)
            N_choice = self.q._user_input(downgrade_list, downgrade=True)
        self.assertEqual(N_choice, -2)

    @unittest.skipUnless(device_connected_dom0(), REQUIRED_DEV)
    def test_update_firmware_dom0(self):
        old_version = None
        new_version = None
        self.q._get_dom0_updates()
        self.q._parse_dom0_updates_info(self.q.dom0_updates_info)
        for number, device in enumerate(self.q.dom0_updates_list):
            if "Name" not in device:
                continue
            if device["Name"] == "ColorHug2":
                old_version = device["Version"]
                break
        if old_version is None:
            self.fail("Test device not found")
        user_input = [str(number + 1)]
        with patch("builtins.input", side_effect=user_input):
            self.q.update_firmware()
        self.q._get_dom0_devices()
        dom0_devices_info_dict = json.loads(self.q.dom0_devices_info)
        for device in dom0_devices_info_dict["Devices"]:
            if "Name" not in device:
                continue
            if device["Name"] == "ColorHug2":
                new_version = device["Version"]
                break
        if new_version is None:
            self.fail("Test device not found")
        self.assertEqual(self.q._compare_versions(old_version, new_version), -1)


if __name__ == "__main__":
    unittest.main()
