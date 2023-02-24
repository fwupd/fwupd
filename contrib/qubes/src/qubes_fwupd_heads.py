#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import subprocess
import tempfile
import os
import shutil
import xml.etree.ElementTree as ET
from packaging.version import Version
from qubes_fwupd_common import EXIT_CODES, create_dirs, LooseVersion

FWUPDTOOL = "/bin/fwupdtool"

BOOT = "/boot"
HEADS_UPDATES_DIR = os.path.join(BOOT, "updates")


class FwupdHeads:
    def _get_hwids(self):
        cmd_hwids = [FWUPDTOOL, "hwids"]
        p = subprocess.Popen(cmd_hwids, stdout=subprocess.PIPE)
        self.dom0_hwids_info = p.communicate()[0].decode()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Getting hwids info failed")

    def _gather_firmware_version(self):
        """
        Checks if Qubes works under heads
        """
        if "Heads" in self.dom0_hwids_info:
            self.heads_version = None
            hwids = self.dom0_hwids_info.split("\n")
            for line in hwids:
                if "Heads" in line:
                    self.heads_version = line.split("Heads-v")[1]
        else:
            print("Device is not running under the heads firmware!!")
            print("Exiting...")
            return EXIT_CODES["NOTHING_TO_DO"]

    def _get_hwid_device(self):
        """
        Device model for Heads update, currently supports ThinkPad only.
        """
        for line in self.dom0_hwids_info.splitlines():
            if line.startswith("Family: ThinkPad"):
                return line.split(":", 1)[1].split(" ", 1)[1].lower()
        return None

    def _parse_metadata(self, metadata_file):
        """
        Parse metadata info.
        """
        metadata_ext = os.path.splitext(metadata_file)[-1]
        if metadata_ext == ".xz":
            cmd_metadata = ["xzcat", metadata_file]
        elif metadata_ext == ".gz":
            cmd_metadata = ["zcat", metadata_file]
        else:
            raise NotImplementedError(
                "Unsupported metadata compression " + metadata_ext
            )
        p = subprocess.Popen(cmd_metadata, stdout=subprocess.PIPE)
        self.metadata_info = p.communicate()[0].decode()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Parsing metadata failed")

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
            if self.heads_version == "heads" or LooseVersion(
                release_ver
            ) > LooseVersion(self.heads_version):
                if not self.heads_update_version or LooseVersion(
                    release_ver
                ) > LooseVersion(self.heads_update_version):
                    self.heads_update_url = release.find("location").text
                    for sha in release.findall("checksum"):
                        if (
                            ".cab" in sha.attrib["filename"]
                            and sha.attrib["type"] == "sha256"
                        ):
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
        heads_boot_path = os.path.join(HEADS_UPDATES_DIR, self.heads_update_version)

        heads_update_path = os.path.join(heads_boot_path, "firmware.rom")
        create_dirs(HEADS_UPDATES_DIR)
        if os.path.exists(heads_update_path):
            print(f"Heads Update == {self.heads_update_version} " "already downloaded.")
            return EXIT_CODES["NOTHING_TO_DO"]
        else:
            os.mkdir(heads_boot_path)
            with tempfile.TemporaryDirectory() as tmpdir:
                cmd_extract = ["gcab", "-x", f"--directory={tmpdir}", "--", arch_path]
                p = subprocess.Popen(cmd_extract, stdout=subprocess.PIPE)
                p.communicate()
                if p.returncode != 0:
                    raise Exception(f"gcab: Error while extracting {arch_path}.")
                update_path = os.path.join(tmpdir, "firmware.rom")
                shutil.copyfile(update_path, heads_update_path)
            print(
                f"Heads Update == {self.heads_update_version} "
                f"available at {heads_boot_path}"
            )
            return EXIT_CODES["SUCCESS"]
