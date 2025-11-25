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

try:
    from jinja2 import Environment, Template
except ImportError:
    print(
        "Error: jinja2 is required for this script. Install it with: pip install jinja2"
    )
    exit(1)


@contextlib.contextmanager
def cd(path):
    prev_cwd = os.getcwd()
    os.chdir(path)
    yield
    os.chdir(prev_cwd)


# Embedded Jinja2 template for firmware.metainfo.xml
FIRMWARE_METAINFO_TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<component type="firmware">
  <id>org.{{ developer_name }}.guid{{ firmware_id }}</id>
  <name>{{ firmware_name }}</name>{% if name_variant_suffix %}
  <name_variant_suffix>{{ name_variant_suffix }}</name_variant_suffix>{% endif %}
  <summary>{{ firmware_summary }}</summary>
  <description>
    <p>{{ firmware_description }}</p>
  </description>
  <provides>
    <firmware type="flashed">{{ device_guid }}</firmware>
  </provides>
  <url type="homepage">{{ firmware_homepage }}</url>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>proprietary</project_license>
  <developer_name>{{ developer_name }}</developer_name>
  <releases>
    <release urgency="{{ release_urgency }}" version="{{ release_version }}" date="{{ date }}">
      <checksum filename="firmware.bin" target="content"/>
      <description>
        <p>{{ release_description }}</p>
        {% for feature in release_features %}
        <p>{{ feature }}</p>
        {% endfor %}
      </description>
    </release>
  </releases>
  <custom>
    <value key="LVFS::VersionFormat">{{ version_format }}</value>
    <value key="LVFS::UpdateProtocol">{{ update_protocol }}</value>
  </custom>
  <categories>
    {% for category in categories %}
    <category>{{ category }}</category>
    {% endfor %}
  </categories>
</component>"""


def load_template():
    """Load the Jinja2 template from the embedded string with XML autoescape."""
    env = Environment(autoescape=True)
    return env.from_string(FIRMWARE_METAINFO_TEMPLATE)


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

    # Parse release features into a list for template iteration
    if "release_features" in local_info and local_info["release_features"]:
        features = local_info["release_features"]
        # Handle both string (with | delimiter) and list inputs
        if isinstance(features, str):
            features = features.split("|")
        elif isinstance(features, list):
            # If it's already a list, use it directly
            pass
        else:
            features = []

        # Clean up features and filter out empty ones
        cleaned_features = []
        for feature in features:
            feature = feature.strip()
            if feature:  # Only add non-empty features
                cleaned_features.append(feature)

        local_info["release_features"] = cleaned_features
    else:
        local_info["release_features"] = []

    # Parse firmware categories into a list for template iteration
    if "firmware_category" in local_info and local_info["firmware_category"]:
        categories = local_info["firmware_category"]
        # Handle both string (with | delimiter) and list inputs
        if isinstance(categories, str):
            categories = categories.split("|")
        elif isinstance(categories, list):
            # Flatten the list in case we have nested lists from argparse append + pipe-separated values
            flattened_categories = []
            for item in categories:
                if isinstance(item, str) and "|" in item:
                    flattened_categories.extend(item.split("|"))
                else:
                    flattened_categories.append(item)
            categories = flattened_categories
        else:
            categories = ["X-System"]  # Default fallback

        # Clean up categories and filter out empty ones
        cleaned_categories = []
        for category in categories:
            category = category.strip()
            if category:  # Only add non-empty categories
                cleaned_categories.append(category)

        local_info["categories"] = (
            cleaned_categories if cleaned_categories else ["X-System"]
        )
    else:
        # Default category if none provided
        local_info["categories"] = ["X-System"]

    # Load and render the Jinja2 template
    template = load_template()
    firmware_metainfo = template.render(**local_info, date=time.strftime("%Y-%m-%d"))

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
        help="Features in the release feature list. Accept multiple calls or one call splitting them with |",
        action="append",
        default=[],
    )
    parser.add_argument(
        "--release-urgency",
        help="Release urgency (e.g. low, medium, high, critical)",
        choices=["low", "medium", "high", "critical"],
        default="medium",
    )
    parser.add_argument(
        "--firmware-category",
        help="Firmware category (e.g. X-System, X-Device, X-EmbeddedController, ...) Split them with | or use multiple --firmware-category arguments",
        action="append",
        choices=[
            "X-System",
            "X-Device",
            "X-EmbeddedController",
            "X-ManagementEngine",
            "X-Controller",
            "X-CorporateManagementEngine",
            "X-ConsumerManagementEngine",
            "X-ThunderboltController",
            "X-PlatformSecurityProcessor",
            "X-CpuMicrocode",
            "X-Configuration",
            "X-Battery",
            "X-Camera",
            "X-TPM",
            "X-Touchpad",
            "X-Mouse",
            "X-Keyboard",
            "X-StorageController",
            "X-NetworkInterface",
            "X-VideoDisplay",
            "X-BaseboardManagementController",
            "X-UsbReceiver",
            "X-Drive",
            "X-FlashDrive",
            "X-SolidStateDrive",
            "X-Gpu",
            "X-Dock",
            "X-UsbDock",
            "X-FingerprintReader",
            "X-GraphicsTablet",
            "X-InputController",
            "X-Headphones",
            "X-Headset",
        ],
        default=[],
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
