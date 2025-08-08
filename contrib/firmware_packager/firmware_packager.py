#!/usr/bin/env python3
#
# Copyright 2017 Max Ehrlich maxehr@gmail.com
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import argparse
import contextlib
import os
import shutil
import subprocess
import tempfile
import time


@contextlib.contextmanager
def cd(path):
    prev_cwd = os.getcwd()
    os.chdir(path)
    yield
    os.chdir(prev_cwd)


firmware_metainfo_template = """<?xml version="1.0" encoding="UTF-8"?>
<component type="firmware">
  <id>org.{developer_name}.guid{firmware_id}</id>
  <name>{firmware_name}</name>
  <name_variant_suffix>{name_variant_suffix}</name_variant_suffix>
  <summary>{firmware_summary}</summary>
  <description>
    <p>{firmware_description}</p>
  </description>
  <provides>
    <firmware type="flashed">{device_guid}</firmware>
  </provides>
  <url type="homepage">{firmware_homepage}</url>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>proprietary</project_license>
  <developer_name>{developer_name}</developer_name>
  <releases>
    <release urgency="{release_urgency}" version="{release_version}" date="{date}">
      <checksum filename="firmware.bin" target="content"/>
      <description>
        <p>{release_description}</p>
        <p>{release_features}</p>
      </description>
    </release>
  </releases>
  <custom>
    <value key="LVFS::VersionFormat">{version_format}</value>
    <value key="LVFS::UpdateProtocol">{update_protocol}</value>
    <value key="LVFS::RequireTest">true</value>
  </custom>
  <categories>
    <category>{firmware_category}</category>
  </categories>
</component>
"""


def make_firmware_metainfo(firmware_info, dst):
    local_info = vars(firmware_info)
    local_info["firmware_id"] = local_info["device_guid"][0:8]
    # Convert name-variant-suffix to name_variant_suffix for template
    if "name_variant_suffix" not in local_info and hasattr(
        firmware_info, "name_variant_suffix"
    ):
        local_info["name_variant_suffix"] = getattr(
            firmware_info, "name_variant_suffix", ""
        )
    # Format release features as a single paragraph string
    if "release_features" in local_info and local_info["release_features"]:
        local_info["release_features"] = "; ".join(local_info["release_features"])
    else:
        local_info["release_features"] = ""
    firmware_metainfo = firmware_metainfo_template.format(
        **local_info, date=time.strftime("%Y-%m-%d")
    )

    with open(os.path.join(dst, "firmware.metainfo.xml"), "w") as f:
        f.write(firmware_metainfo)


def extract_exe(exe, dst):
    command = ["7z", "x", f"-o{dst}", exe]
    subprocess.check_call(command, stdout=subprocess.DEVNULL)


def get_firmware_bin(root, bin_path, dst):
    with cd(root):
        shutil.copy(bin_path, os.path.join(dst, "firmware.bin"))


def create_firmware_cab(exe, folder):
    with cd(folder):
        if os.name == "nt":
            directive = os.path.join(folder, "directive")
            with open(directive, "w") as wfd:
                wfd.write(".OPTION EXPLICIT\r\n")
                wfd.write(".Set CabinetNameTemplate=firmware.cab\r\n")
                wfd.write(".Set DiskDirectory1=.\r\n")
                wfd.write("firmware.bin\r\n")
                wfd.write("firmware.metainfo.xml\r\n")
            command = ["makecab.exe", "/f", directive]
        else:
            command = [
                "fwupdtool",
                "build-cabinet",
                "firmware.cab",
                "firmware.bin",
                "firmware.metainfo.xml",
            ]
        subprocess.check_call(command)


def main(args):
    with tempfile.TemporaryDirectory() as d:
        print(f"Using temp directory {d}")

        if args.exe:
            print("Extracting firmware exe")
            extract_exe(args.exe, d)

        print("Locating firmware bin")
        get_firmware_bin(d, args.bin, d)

        print("Creating metainfo")
        make_firmware_metainfo(args, d)

        print("Creating cabinet file")
        create_firmware_cab(args, d)

        print("Done")
        shutil.copy(os.path.join(d, "firmware.cab"), args.out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Create fwupd packaged from windows executables"
    )
    parser.add_argument(
        "--firmware-name",
        help="Name of the firmware package can be customized (e.g. DellTBT)",
        required=True,
    )
    parser.add_argument(
        "--name-variant-suffix",
        help="Name variant suffix for the firmware package",
        default="",
    )
    parser.add_argument(
        "--firmware-summary", help="One line description of the firmware package"
    )
    parser.add_argument(
        "--firmware-description", help="Longer description of the firmware package"
    )
    parser.add_argument(
        "--device-guid",
        help="GUID of the device this firmware will run on, this *must* match the output of one of the GUIDs in `fwupdmgr get-devices`",
        required=True,
    )
    parser.add_argument("--firmware-homepage", help="Website for the firmware provider")
    parser.add_argument(
        "--developer-name", help="Name of the firmware developer", required=True
    )
    parser.add_argument(
        "--release-version",
        help="Version number of the firmware package",
        required=True,
    )
    parser.add_argument(
        "--version-format",
        help="Version format, e.g. quad or triplet",
        required=True,
    )
    parser.add_argument(
        "--update-protocol",
        help="Update protocol, e.g. org.uefi.capsule",
        required=True,
    )
    parser.add_argument(
        "--update-message",
        help="Update message for LVFS compliance",
        default="This firmware has been tested on target hardware and verified to work correctly",
    )
    parser.add_argument(
        "--release-description", help="Description of the firmware release"
    )
    parser.add_argument(
        "--release-features",
        help="Features in the release feature list",
        action="append",
        default=[],
    )
    parser.add_argument(
        "--release-urgency",
        help="Release urgency (e.g. low, medium, high, critical)",
        default="medium",
    )
    parser.add_argument(
        "--firmware-category",
        help="Firmware category (e.g. X-System, X-Device, X-EmbeddedController)",
        default="X-System",
    )
    parser.add_argument(
        "--exe", help="(optional) Executable file to extract firmware from"
    )
    parser.add_argument(
        "--bin",
        help="Path to the .bin file (Relative if inside the executable; Absolute if outside) to use as the firmware image",
        required=True,
    )
    parser.add_argument("--out", help="Output cab file path", required=True)
    args = parser.parse_args()

    main(args)
