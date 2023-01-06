#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kaminski  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

from pathlib import Path
from packaging import version as pversion

FWUPD_QUBES_DIR = "/usr/share/qubes-fwupd"

# Check if script is run by tests and append sys path properly
if __name__ == "__main__":
    sys.path.append(os.path.join(FWUPD_QUBES_DIR, "src"))
else:
    sys.path.append("./src")

try:
    from qubes_fwupd_heads import FwupdHeads
    from qubes_fwupd_update import FwupdUpdate, run_in_tty
    from fwupd_receive_updates import FwupdReceiveUpdates
except ModuleNotFoundError:
    raise ModuleNotFoundError(
        "qubes-fwupd modules not found.  You may need to reinstall package."
    )

FWUPD_DOM0_DIR = "/var/cache/fwupd/qubes"
FWUPD_DOM0_METADATA_DIR = os.path.join(FWUPD_DOM0_DIR, "metadata")
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOM0_METADATA_SIGNATURE = os.path.join(
    FWUPD_DOM0_METADATA_DIR, "firmware.xml.gz.asc"
)
FWUPD_DOM0_METADATA_FILE = os.path.join(FWUPD_DOM0_METADATA_DIR, "firmware.xml.gz")
FWUPD_DOM0_METADATA_JCAT = os.path.join(FWUPD_DOM0_METADATA_DIR, "firmware.xml.gz.jcat")
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"

FWUPDMGR = "/bin/fwupdmgr"

BIOS_UPDATE_FLAG = os.path.join(FWUPD_DOM0_DIR, "bios_update")
LVFS_TESTING_DOM0_FLAG = os.path.join(FWUPD_DOM0_DIR, "lvfs_testing")

HELP = {
    "Usage": [
        {
            "Command": "qubes-fwupdmgr [OPTION…][FLAG..]",
            "Example": "qubes-fwupdmgr refresh --whonix --url=<url>\n",
        }
    ],
    "Options": [
        {
            "get-devices": "Get all devices that support firmware updates",
            "get-updates": "Get the list of updates for connected hardware",
            "refresh": "Refresh metadata from remote server",
            "update": "Update chosen device to latest firmware version",
            "update-heads": "Updates heads firmware to the latest version",
            "downgrade": "Downgrade chosen device to chosen firmware version",
            "clean": "Delete all cached update files\n",
        }
    ],
    "Flags": [
        {
            "--whonix": "Download firmware updates via Tor",
            "--device": "Specify device for heads update (default - x230)",
            "--url": "Address of the custom metadata remote server\n",
        }
    ],
    "Help": [{"-h --help": "Show help options\n"}],
}

EXIT_CODES = {"ERROR": 1, "SUCCESS": 0, "NOTHING_TO_DO": 2}


class QubesFwupdmgr(FwupdHeads, FwupdUpdate, FwupdReceiveUpdates):
    def _download_metadata(self, whonix=False, metadata_url=None):
        """Initialize downloading metadata files.

        Keywords arguments:
        whonix -- Flag enforces downloading the metadata updates via Tor
        metadata_url -- Download metadata from the custom url
        """
        self.download_metadata(whonix=whonix, metadata_url=metadata_url)
        self.handle_metadata_update(self.updatevm, metadata_url=metadata_url)
        if not os.path.exists(self.metadata_file):
            raise FileNotFoundError("Metadata file does not exist")

    def refresh_metadata(self, whonix=False, metadata_url=None):
        """Updates metadata with downloaded files.

        Keyword arguments:
        whonix -- Flag enforces downloading the metadata updates via Tor
        metadata_url -- Use custom metadata from the url
        """
        if metadata_url:
            metadata_name = metadata_url.replace(FWUPD_DOWNLOAD_PREFIX, "")
            self.metadata_file = os.path.join(FWUPD_DOM0_METADATA_DIR, metadata_name)
            self.metadata_file_jcat = self.metadata_file + ".jcat"
            self.lvfs = "lvfs-testing"
        else:
            self.metadata_file = FWUPD_DOM0_METADATA_FILE
            self.metadata_file_jcat = FWUPD_DOM0_METADATA_JCAT
            self.lvfs = "lvfs"
        self._download_metadata(whonix=whonix, metadata_url=metadata_url)
        cmd_refresh = [
            FWUPDMGR,
            "refresh",
            self.metadata_file,
            self.metadata_file_jcat,
            self.lvfs,
        ]
        p = subprocess.Popen(cmd_refresh, stdout=subprocess.PIPE)
        output = p.communicate()[0].decode()
        print(output)
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Refresh failed")
        if not output != "Successfully refreshed metadata manually":
            raise Exception("Manual metadata refresh failed!!!")

    def _get_dom0_updates(self):
        """Gathers information about available updates."""
        cmd_get_dom0_updates = [FWUPDMGR, "--json", "get-updates"]
        p = subprocess.Popen(cmd_get_dom0_updates, stdout=subprocess.PIPE)
        self.dom0_updates_info = p.communicate()[0].decode()
        if p.returncode != 0 and p.returncode != 2:
            raise Exception("fwupd-qubes: Getting available updates failed")

    def _parse_dom0_updates_info(self, updates_info):
        """Creates dictionary and list with information about updates.

        Keywords argument:
        updates_info - gathered update information
        """
        self.dom0_updates_info_dict = json.loads(updates_info)
        self.dom0_updates_list = [
            {
                "Name": device["Name"],
                "Version": device["Version"],
                "Releases": [
                    {
                        "Version": update["Version"],
                        "Url": update["Uri"],
                        "Checksum": update["Checksum"][-1],
                        "Description": update["Description"],
                    }
                    for update in device["Releases"]
                ],
            }
            for device in self.dom0_updates_info_dict["Devices"]
        ]

    def _download_firmware_updates(self, url, sha, whonix=False):
        """Initializes downloading firmware update archive.

        Keywords arguments:
        url -- url path to the firmware update archive
        sha -- SHA256 checksum of the firmware update archive
        whonix -- Flag enforces downloading the updates via Tor
        """
        self.cached = False
        self.download_firmware_updates(url, sha, whonix=whonix)
        if not self.cached:
            self.handle_fw_update(self.updatevm, sha, self.arch_name)
        update_path = self.arch_path.replace(".cab", "")
        if not os.path.exists(update_path):
            raise NotADirectoryError("Firmware update files do not exist")

    def _user_input(self, updates_list, downgrade=False):
        """UI for update process.

        Keywords arguments:
        updates_dict - list of updates for specified device
        downgrade -- downgrade flag
        """
        decorator = "======================================================"
        if len(updates_list) == 0:
            print("No updates available.")
            return -EXIT_CODES["NOTHING_TO_DO"]
        if downgrade:
            print("Available downgrades:")
        else:
            print("Available updates:")
        self._updates_crawler(updates_list)

        while True:
            try:
                print("If you want to abandon process press 'N'.")
                choice = input("Otherwise choose a device number: ")
                if choice == "N" or choice == "n":
                    return -EXIT_CODES["NOTHING_TO_DO"]
                device_num = int(choice) - 1
                if 0 <= device_num < len(updates_list):
                    if not downgrade:
                        return device_num
                    break
                else:
                    raise ValueError()
            except ValueError:
                print("Invalid choice.")

        if downgrade:
            while True:
                try:
                    releases = updates_list[device_num]["Releases"]
                    for i, fw_dngd in enumerate(releases):
                        print(decorator)
                        print(
                            f"  {i+1}. Firmware downgrade version:"
                            f"\t {fw_dngd['Version']}"
                        )
                        description = fw_dngd["Description"].replace("<p>", "")
                        description = description.replace("<li>", "")
                        description = description.replace("<ul>", "")
                        description = description.replace("</ul>", "")
                        description = description.replace("</p>", "\n   ")
                        description = description.replace("</li>", "\n   ")
                        print(f"   Description:{description}")
                    print("If you want to abandon downgrade process press N.")
                    choice = input("Otherwise choose downgrade number: ")
                    if choice == "N" or choice == "n":
                        return -EXIT_CODES["NOTHING_TO_DO"]
                    downgrade_num = int(choice) - 1
                    if 0 <= downgrade_num < len(releases):
                        return device_num, downgrade_num
                    else:
                        raise ValueError()
                except ValueError:
                    print("Invalid choice.")

    def _parse_parameters(self, updates_list, choice):
        """Parses device name, url, version and SHA256 checksum of the file list.

        Keywords arguments:
        updates_list - list of updates for dom0
        choice -- number of device to be updated
        """
        self.name = updates_list[choice]["Name"]
        self.version = updates_list[choice]["Releases"][0]["Version"]
        for ver_check in updates_list[choice]["Releases"]:
            if pversion.parse(ver_check["Version"]) >= pversion.parse(self.version):
                self.version = ver_check["Version"]
                self.url = ver_check["Url"]
                self.sha = ver_check["Checksum"]

    def _install_dom0_firmware_update(self, arch_path):
        """Installs firmware update for specified device in dom0.

        Keywords arguments:
        arch_path - absolute path to firmware update archive
        """
        cmd_install = [FWUPDMGR, "install", arch_path]
        p = subprocess.Popen(cmd_install)
        p.wait()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Firmware update failed")

    def _read_dmi(self):
        """Reads BIOS information from DMI."""
        cmd_dmidecode_version = ["dmidecode", "-s", "bios-version"]
        p = subprocess.Popen(cmd_dmidecode_version, stdout=subprocess.PIPE)
        p.wait()
        self.dmi_version = p.communicate()[0].decode()
        cmd_dmidecode = ["dmidecode", "-t", "bios"]
        p = subprocess.Popen(cmd_dmidecode, stdout=subprocess.PIPE)
        p.wait()
        if p.returncode != 0:
            raise Exception("dmidecode: Reading DMI failed")
        return p.communicate()[0].decode()

    def _verify_dmi(self, path, version, downgrade=False):
        """Verifies DMI tables for BIOS updates.

        Keywords arguments:
        path -- absolute path of the updates files
        version -- version of the update
        downgrade -- downgrade flag
        """
        dmi_info = self._read_dmi()
        path_metainfo = os.path.join(path, "firmware.metainfo.xml")
        tree = ET.parse(path_metainfo)
        root = tree.getroot()
        vendor = root.find("developer_name").text
        if vendor is None:
            raise ValueError("No vendor information in firmware metainfo.")
        if vendor not in dmi_info:
            raise ValueError("Wrong firmware provider.")
        if not downgrade and pversion.parse(version) <= pversion.parse(
            self.dmi_version
        ):
            raise ValueError(f"{version} < {self.dmi_version} Downgrade not allowed")

    def _get_dom0_devices(self):
        """Gathers information about devices connected in dom0."""
        cmd_get_dom0_devices = [FWUPDMGR, "--json", "get-devices"]
        p = subprocess.Popen(cmd_get_dom0_devices, stdout=subprocess.PIPE)
        self.dom0_devices_info = p.communicate()[0].decode()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Getting devices info failed")

    def update_firmware(self, whonix=False):
        """Updates firmware of the specified device.

        Keyword arguments:
        whonix -- Flag enforces downloading the metadata updates via Tor
        """
        self._get_dom0_updates()
        self._parse_dom0_updates_info(self.dom0_updates_info)
        updates_list = self.dom0_updates_list
        ret_input = self._user_input(updates_list)
        if ret_input == -EXIT_CODES["NOTHING_TO_DO"]:
            exit(EXIT_CODES["NOTHING_TO_DO"])
        choice = ret_input
        self._parse_parameters(updates_list, choice)
        self._download_firmware_updates(self.url, self.sha, whonix=whonix)
        if self.name == "System Firmware":
            Path(BIOS_UPDATE_FLAG).touch(mode=0o644, exist_ok=True)
            extracted_path = self.arch_path.replace(".cab", "")
            self._verify_dmi(extracted_path, self.version)
        self._install_dom0_firmware_update(self.arch_path)

    def _parse_downgrades(self, device_list):
        """Parses information about possible downgrades.

        Keywords argument:
        device_list -- list of connected devices
        """
        downgrades = []
        if "No detected devices" in device_list:
            return downgrades
        dom0_devices_info_dict = json.loads(device_list)
        for device in dom0_devices_info_dict["Devices"]:
            if "Releases" in device:
                try:
                    version = device["Version"]
                except KeyError:
                    continue
                downgrades.append(
                    {
                        "Name": device["Name"],
                        "Version": device["Version"],
                        "Releases": [
                            {
                                "Version": downgrade["Version"],
                                "Description": downgrade["Description"],
                                "Url": downgrade["Uri"],
                                "Checksum": downgrade["Checksum"][-1],
                            }
                            for downgrade in device["Releases"]
                            if pversion.parse(downgrade["Version"])
                            < pversion.parse(version)
                        ],
                    }
                )
        return downgrades

    def _install_dom0_firmware_downgrade(self, arch_path):
        """Installs firmware downgrade for specified device.

        Keywords arguments:
        arch_path - absolute path to firmware downgrade archive
        """
        cmd_install = [FWUPDMGR, "--allow-older", "install", arch_path]
        p = subprocess.Popen(cmd_install)
        p.wait()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Firmware downgrade failed")

    def downgrade_firmware(self, whonix=False):
        """Downgrades firmware of the specified device.

        Keyword arguments:
        whonix -- Flag enforces downloading the metadata updates via Tor
        """
        self._get_dom0_devices()
        dom0_downgrades = self._parse_downgrades(self.dom0_devices_info)
        ret_input = self._user_input(dom0_downgrades, downgrade=True)
        if ret_input == -EXIT_CODES["NOTHING_TO_DO"]:
            exit(EXIT_CODES["NOTHING_TO_DO"])
        device_choice, downgrade_choice = ret_input
        downgrade = dom0_downgrades[device_choice]
        releases = downgrade["Releases"]
        downgrade_url = releases[downgrade_choice]["Url"]
        downgrade_sha = releases[downgrade_choice]["Checksum"]
        self._download_firmware_updates(downgrade_url, downgrade_sha, whonix=whonix)
        if downgrade["Name"] == "System Firmware":
            Path(BIOS_UPDATE_FLAG).touch(mode=0o644, exist_ok=True)
            extracted_path = self.arch_path.replace(".cab", "")
            self._verify_dmi(
                extracted_path,
                downgrade["Version"],
                downgrade=True,
            )
        self._install_dom0_firmware_downgrade(self.arch_path)

    def _output_crawler(self, updev_dict, level, help_f=False):
        """Prints device and updates information as a tree.

        Keywords arguments:
        updev_dict -- update/device information dictionary
        level -- level of the tree
        """

        def _tabs(key_word):
            return key_word + "\t" * (4 - int(len(key_word) / 8))

        decorator = "==================================="
        print(2 * decorator)
        for updev_key in updev_dict:
            style = "\t" * level
            output = style + _tabs(updev_key + ":")
            if len(updev_key) > 12:
                continue
            if updev_key == "Icons":
                continue
            if updev_key == "Releases":
                continue
            if updev_key == "Name":
                print(style + updev_dict["Name"])
                print(2 * decorator)
                continue
            if isinstance(updev_dict[updev_key], str):
                print(output + updev_dict[updev_key])
            elif isinstance(updev_dict[updev_key], int):
                print(output + str(updev_dict[updev_key]))
            elif isinstance(updev_dict[updev_key][0], str):
                for i, data in enumerate(updev_dict[updev_key]):
                    if i == 0:
                        print(output + "\u00B7" + data)
                        continue
                    print(style + _tabs(" ") + "\u00B7" + data)
            elif isinstance(updev_dict[updev_key][0], dict):
                if level == 0 and help_f is True:
                    print(output)
                else:
                    if level == 0:
                        print(f"Dom0 {output}")

                for nested_dict in updev_dict[updev_key]:
                    self._output_crawler(nested_dict, level + 1)

    def _updates_crawler(self, updates_list, prefix=0):
        """Prints updates information for dom0

        Keywords arguments:
        updates_list -- list of devices updates
        prefix -- device number prefix
        """
        available_updates = False
        decorator = "======================================================"
        print(decorator)
        print("Dom0 updates:")
        print(decorator)
        if len(updates_list) == 0:
            print("No updates available.")
            return EXIT_CODES["NOTHING_TO_DO"]
        else:
            for i, device in enumerate(updates_list):
                if len(device["Releases"]) == 0:
                    continue
                if not available_updates:
                    print("Available updates:")
                    print(decorator)
                print("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^")
                print(f"{i+1+prefix}. Device: {device['Name']}")
                print(f"   Current firmware version:\t {device['Version']}")
                for update in device["Releases"]:
                    print(decorator)
                    print("   Firmware update " f"version:\t {update['Version']}")
                    print(f"   URL:\t {update['Url']}")
                    print(f"   SHA256 checksum:\t {update['Checksum']}")
                    description = update["Description"].replace("<p>", "")
                    description = description.replace("<li>", "")
                    description = description.replace("<ul>", "")
                    description = description.replace("</ul>", "")
                    description = description.replace("</p>", "\n\t")
                    description = description.replace("</li>", "\n\t")
                    print(f"   Description: {description}")
                print(decorator)
                available_updates = True
            if not available_updates:
                print("No updates available.")
                return EXIT_CODES["NOTHING_TO_DO"]

    def get_devices_qubes(self):
        """Gathers and prints devices information."""
        self._get_dom0_devices()
        dom0_devices_info_dict = json.loads(self.dom0_devices_info)
        self._output_crawler(dom0_devices_info_dict, 0)

    def get_updates_qubes(self):
        """Gathers and prints updates information."""
        self._get_dom0_updates()
        self._parse_dom0_updates_info(self.dom0_updates_info)
        self._updates_crawler(self.dom0_updates_list)

    def help(self):
        """Prints help information"""
        self._output_crawler(HELP, 0, help_f=True)

    def trusted_cleanup(self):
        """Deletes trusted directory."""
        trusted_path = os.path.join(FWUPD_DOM0_UPDATES_DIR, "trusted.cab")
        if os.path.exists(trusted_path):
            os.remove(trusted_path)
            shutil.rmtree(trusted_path.replace(".cab", ""))

    def refresh_metadata_after_bios_update(self):
        """Refreshes metadata after bios update"""
        if os.path.exists(BIOS_UPDATE_FLAG):
            print("BIOS was updated. Refreshing metadata...")
            if "--whonix" in sys.argv:
                self.refresh_metadata(whonix=True)
            else:
                self.refresh_metadata()
            os.remove(BIOS_UPDATE_FLAG)

    def heads_update(self, device="x230", whonix=False, metadata_url=None):
        """
        Updates heads firmware

        Keyword arguments:
        device -- Model of the updated device
        whonix -- Flag enforces downloading the metadata updates via Tor
        metadata_url -- Use custom metadata from the url
        """
        if metadata_url:
            custom_metadata_name = metadata_url.replace(FWUPD_DOWNLOAD_PREFIX, "")
            self.metadata_file = os.path.join(
                FWUPD_DOM0_METADATA_DIR, custom_metadata_name
            )
        else:
            self.metadata_file = FWUPD_DOM0_METADATA_FILE
        self._get_hwids()
        self._download_metadata(whonix=whonix, metadata_url=metadata_url)
        self._parse_metadata(self.metadata_file)
        if self._gather_firmware_version() == EXIT_CODES["NOTHING_TO_DO"]:
            return EXIT_CODES["NOTHING_TO_DO"]
        if self._parse_heads_updates(device) == EXIT_CODES["NOTHING_TO_DO"]:
            return EXIT_CODES["NOTHING_TO_DO"]
        self._download_firmware_updates(self.heads_update_url, self.heads_update_sha)
        return_code = self._copy_heads_firmware(self.arch_path)
        if return_code == EXIT_CODES["NOTHING_TO_DO"]:
            exit(EXIT_CODES["NOTHING_TO_DO"])
        elif return_code == EXIT_CODES["SUCCESS"]:
            print()
            while True:
                try:
                    print("An update requires a reboot to complete.")
                    choice = input("Do you want to restart now? (Y|N)\n")
                    if choice == "N" or choice == "n":
                        return EXIT_CODES["SUCCESS"]
                    elif choice == "Y" or choice == "y":
                        print("Rebooting...")
                        os.system("reboot")
                    else:
                        raise ValueError()
                except ValueError:
                    print("Invalid choice.")
        else:
            raise Exception("Copying heads update failed!!")

    def validate_dom0_dirs(self):
        """Validates and creates directories"""
        if not os.path.exists(FWUPD_DOM0_DIR):
            self._create_dirs(FWUPD_DOM0_DIR)
        if os.path.exists(FWUPD_DOM0_METADATA_DIR):
            shutil.rmtree(FWUPD_DOM0_METADATA_DIR)
            self._create_dirs(FWUPD_DOM0_METADATA_DIR)
        else:
            self._create_dirs(FWUPD_DOM0_METADATA_DIR)
        if not os.path.exists(FWUPD_DOM0_UPDATES_DIR):
            self._create_dirs(FWUPD_DOM0_UPDATES_DIR)
        os.umask(self.old_umask)


def main():
    if os.geteuid() != 0:
        print("You need to have root privileges to run this script.\n")
        exit(EXIT_CODES["ERROR"])

    q = QubesFwupdmgr()

    if len(sys.argv) < 2:
        q.help()
        exit(1)

    metadata_url = None
    device = "x230"
    whonix = False

    for arg in sys.argv:
        if "--url=" in arg:
            metadata_url = arg.replace("--url=", "")
            if FWUPD_DOWNLOAD_PREFIX not in metadata_url:
                print(
                    "Metadata must be stored in the Linux"
                    " Vendor Firmware Service (https://fwupd.org/)"
                )
                print("Exiting...")
                exit(1)
        if "--device=" in arg:
            device = arg.replace("--device=", "")
        if "--whonix" == arg:
            whonix = True

    q.validate_dom0_dirs()
    q.trusted_cleanup()
    q.refresh_metadata_after_bios_update()

    if not os.path.exists(FWUPD_DOM0_DIR):
        q.refresh_metadata(whonix=whonix, metadata_url=metadata_url)

    if sys.argv[1] == "get-updates":
        q.get_updates_qubes()
    elif sys.argv[1] == "get-devices":
        q.get_devices_qubes()
    elif sys.argv[1] == "update":
        q.update_firmware(whonix=whonix)
    elif sys.argv[1] == "downgrade":
        q.downgrade_firmware(whonix=whonix)
    elif sys.argv[1] == "clean":
        q.clean_cache()
    elif sys.argv[1] == "refresh":
        q.refresh_metadata(metadata_url=metadata_url, whonix=whonix)
    elif sys.argv[1] == "update-heads":
        q.heads_update(device=device, metadata_url=metadata_url, whonix=whonix)
    else:
        q.help()
        exit(1)


if __name__ == "__main__":
    main()
