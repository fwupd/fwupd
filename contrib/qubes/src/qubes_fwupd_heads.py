#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import subprocess
import os
import shutil
import xml.etree.ElementTree as ET
from distutils.version import LooseVersion as l_ver

FWUPDTOOL = "/bin/fwupdtool"

BOOT = "/boot"
HEADS_UPDATES_DIR = os.path.join(BOOT, "updates")

EXIT_CODES = {
    "ERROR": 1,
    "SUCCESS": 0,
    "NOTHING_TO_DO": 2,
}


class FwupdHeads:
    def _get_hwids(self):
        cmd_hwids = [FWUPDTOOL, "hwids"]
        p = subprocess.Popen(
            cmd_hwids,
            stdout=subprocess.PIPE
        )
        self.dom0_hwids_info = p.communicate()[0].decode()
        if p.returncode != 0:
            raise Exception("fwudp-qubes: Getting hwids info failed")

    def _gather_firmware_version(self):
        """
        Checks if Qubes works under heads
        """
        if "heads" in self.dom0_hwids_info:
            self.heads_version = None
            hwids = self.dom0_hwids_info.split("\n")
            for line in hwids:
                if line.startswith("BiosVersion: CBET4000 "):
                    self.heads_version = line.replace(
                        "BiosVersion: CBET4000 ",
                        ""
                    ).replace(
                        " heads",
                        ""
                    )
        else:
            print("Device is not running under the heads firmware!!")
            print("Exiting...")
            return EXIT_CODES["NOTHING_TO_DO"]

    def _parse_metadata(self, metadata_file):
        """
        Parse metadata info.
        """
        cmd_metadata = ["zcat", metadata_file]
        p = subprocess.Popen(
            cmd_metadata,
            stdout=subprocess.PIPE
        )
        self.metadata_info = p.communicate()[0].decode()
        if p.returncode != 0:
            raise Exception("fwudp-qubes: Parsing metadata failed")

    def _parse_heads_updates(self, device):
        """
        Parses heads updates info.

        Keyword arguments:
        device -- Model of the updated device
        """
        self.heads_update_url = None
        self.heads_update_sha = None
        self.heads_update_version = None
        heads_metadata_info = None
        root = ET.fromstring(self.metadata_info)
        for component in root.findall("component"):
            if f"heads.{device}" in component.find("id").text:
                heads_metadata_info = component
        if not heads_metadata_info:
            print("No metadata info for chosen board")
            return EXIT_CODES["NOTHING_TO_DO"]
        for release in heads_metadata_info.find("releases").findall("release"):
            release_ver = release.get("version")
            if (self.heads_version == "heads" or
                    l_ver(release_ver) > l_ver(self.heads_version)):
                if (not self.heads_update_version or
                        l_ver(release_ver) > l_ver(self.heads_update_version)):
                    self.heads_update_url = release.find("location").text
                    for sha in release.findall("checksum"):
                        if (".cab" in sha.attrib["filename"]
                                and sha.attrib["type"] == "sha256"):
                            self.heads_update_sha = sha.text
                    self.heads_update_version = release_ver
        if self.heads_update_url:
            return EXIT_CODES["SUCCESS"]
        else:
            print("Heads firmware is up to date.")
            return EXIT_CODES["NOTHING_TO_DO"]

    def _copy_heads_firmware(self, arch_path):
        """
        Copies heads update to the boot path
        """
        heads_boot_path = os.path.join(
            HEADS_UPDATES_DIR,
            self.heads_update_version
        )
        update_path = arch_path.replace(".cab", "/firmware.rom")

        heads_update_path = os.path.join(
            heads_boot_path,
            "firmware.rom"
        )
        if not os.path.exists(HEADS_UPDATES_DIR):
            os.mkdir(HEADS_UPDATES_DIR)
        if os.path.exists(heads_update_path):
            print(
                f"Heads Update == {self.heads_update_version} "
                "already downloaded."
            )
            return EXIT_CODES["NOTHING_TO_DO"]
        else:
            os.mkdir(heads_boot_path)
            shutil.copyfile(update_path, heads_update_path)
            print(
                f"Heads Update == {self.heads_update_version} "
                f"available at {heads_boot_path}"
            )
            return EXIT_CODES["SUCCESS"]
