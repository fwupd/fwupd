#!/usr/bin/python3
#
# Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string

import json
import sys
import argparse

from typing import Dict, List, Any

import gi
from gi.repository import GLib

gi.require_version("Fwupd", "2.0")
gi.require_version("Json", "1.0")

from gi.repository import Fwupd  # pylint: disable=wrong-import-position
from gi.repository import Json  # pylint: disable=wrong-import-position

fu_chassis_kind = {
    "FU_SMBIOS_CHASSIS_KIND_OTHER": 0x01,
    "FU_SMBIOS_CHASSIS_KIND_UNKNOWN": 0x02,
    "FU_SMBIOS_CHASSIS_KIND_DESKTOP": 0x03,
    "FU_SMBIOS_CHASSIS_KIND_LOW_PROFILE_DESKTOP": 0x04,
    "FU_SMBIOS_CHASSIS_KIND_PIZZA_BOX": 0x05,
    "FU_SMBIOS_CHASSIS_KIND_MINI_TOWER": 0x06,
    "FU_SMBIOS_CHASSIS_KIND_TOWER": 0x07,
    "FU_SMBIOS_CHASSIS_KIND_PORTABLE": 0x08,
    "FU_SMBIOS_CHASSIS_KIND_LAPTOP": 0x09,
    "FU_SMBIOS_CHASSIS_KIND_NOTEBOOK": 0x0A,
    "FU_SMBIOS_CHASSIS_KIND_HAND_HELD": 0x0B,
    "FU_SMBIOS_CHASSIS_KIND_DOCKING_STATION": 0x0C,
    "FU_SMBIOS_CHASSIS_KIND_ALL_IN_ONE": 0x0D,
    "FU_SMBIOS_CHASSIS_KIND_SUB_NOTEBOOK": 0x0E,
    "FU_SMBIOS_CHASSIS_KIND_SPACE_SAVING": 0x0F,
    "FU_SMBIOS_CHASSIS_KIND_LUNCH_BOX": 0x10,
    "FU_SMBIOS_CHASSIS_KIND_MAIN_SERVER": 0x11,
    "FU_SMBIOS_CHASSIS_KIND_EXPANSION": 0x12,
    "FU_SMBIOS_CHASSIS_KIND_SUBCHASSIS": 0x13,
    "FU_SMBIOS_CHASSIS_KIND_BUS_EXPANSION": 0x14,
    "FU_SMBIOS_CHASSIS_KIND_PERIPHERAL": 0x15,
    "FU_SMBIOS_CHASSIS_KIND_RAID": 0x16,
    "FU_SMBIOS_CHASSIS_KIND_RACK_MOUNT": 0x17,
    "FU_SMBIOS_CHASSIS_KIND_SEALED_CASE_PC": 0x18,
    "FU_SMBIOS_CHASSIS_KIND_MULTI_SYSTEM": 0x19,
    "FU_SMBIOS_CHASSIS_KIND_COMPACT_PCI": 0x1A,
    "FU_SMBIOS_CHASSIS_KIND_ADVANCED_TCA": 0x1B,
    "FU_SMBIOS_CHASSIS_KIND_BLADE": 0x1C,
    "FU_SMBIOS_CHASSIS_KIND_TABLET": 0x1E,
    "FU_SMBIOS_CHASSIS_KIND_CONVERTIBLE": 0x1F,
    "FU_SMBIOS_CHASSIS_KIND_DETACHABLE": 0x20,
    "FU_SMBIOS_CHASSIS_KIND_IOT_GATEWAY": 0x21,
    "FU_SMBIOS_CHASSIS_KIND_EMBEDDED_PC": 0x22,
    "FU_SMBIOS_CHASSIS_KIND_MINI_PC": 0x23,
    "FU_SMBIOS_CHASSIS_KIND_STICK_PC": 0x24,
}


def _minimize_json(json_str: str, machine_kind="physical", chassis_type=0x09) -> str:
    nodes = json.loads(json_str)
    new_attrs: List[Dict[str, Any]] = []
    new_devices: List[Dict[str, Any]] = []
    new_bios_attrs: List[Dict[str, Any]] = []
    try:
        for attr in nodes["SecurityAttributes"]:
            new_attr: Dict[str, Any] = {}
            for key in attr:
                if key in ["AppstreamId", "HsiResult", "Flags", "Plugin"]:
                    new_attr[key] = attr[key]
            new_attrs.append(new_attr)
    except KeyError:
        pass
    try:
        for device in nodes["Devices"]:
            new_device: Dict[str, Any] = {}
            for key in device:
                if key not in ["Created", "Modified", "Releases", "Plugin"]:
                    new_device[key] = device[key]
            new_devices.append(new_device)
    except KeyError:
        pass
    try:
        for device in nodes["BiosAttributes"]:
            new_attr: Dict[str, Any] = {}
            for key in device:
                if key not in ["Filename"]:
                    new_attr[key] = device[key]
            new_bios_attrs.append(new_attr)
    except KeyError:
        pass
    return json.dumps(
        {
            "MachineKind": machine_kind,
            "ChassisType": chassis_type,
            "SecurityAttributes": new_attrs,
            "Devices": new_devices,
            "BiosAttributes": new_bios_attrs,
        },
        indent=2,
        separators=(",", " : "),
    )


def _get_host_devices_and_attrs() -> str:

    # connect to the running daemon
    client = Fwupd.Client()
    builder = Json.Builder()
    builder.begin_object()

    # add devices
    try:
        devices = client.get_devices()
    except GLib.GError as e:
        print("ignoring {}".format(e))
    else:
        builder.set_member_name("Devices")
        builder.begin_array()
        for device in devices:
            builder.begin_object()
            device.to_json_full(builder, Fwupd.DEVICE_FLAG_TRUSTED)
            builder.end_object()
        builder.end_array()

    # add security attributes
    try:
        attrs = client.get_host_security_attrs()
    except GLib.GError as e:
        print("ignoring {}".format(e))
    else:
        builder.set_member_name("SecurityAttributes")
        builder.begin_array()
        for attr in attrs:
            builder.begin_object()
            attr.to_json(builder)
            builder.end_object()
        builder.end_array()

    # add BIOS attributes
    try:
        attrs = client.get_bios_attrs()
    except GLib.GError as e:
        print("ignoring {}".format(e))
    else:
        builder.set_member_name("BiosAttributes")
        builder.begin_array()
        for attr in attrs:
            builder.begin_object()
            attr.to_json(builder)
            builder.end_object()
        builder.end_array()

    # export to JSON
    builder.end_object()
    generator = Json.Generator()
    generator.set_pretty(True)
    generator.set_root(builder.get_root())
    return generator.to_data()[0]


def main():
    if len(sys.argv) < 2:
        sys.stdout.write(_minimize_json(sys.stdin.read()))
    else:
        parser = argparse.ArgumentParser(description="Generate HSI emulation json.")
        parser.add_argument(
            "json_file_list",
            metavar="json_file_list",
            type=str,
            nargs="+",
            help="An list of HSI emulation json file.",
        )
        parser.add_argument(
            "--machine", default="physical", help="Machines kind (default: physical)"
        )
        parser.add_argument(
            "--chassis",
            default="laptop",
            help="Chassis type (laptop and desktop. default: physical)",
        )
        args = parser.parse_args()
        chassis_type = ""
        if args.chassis == "laptop":
            chassis_type = fu_chassis_kind["FU_SMBIOS_CHASSIS_KIND_LAPTOP"]
        elif args.chassis == "desktop":
            chassis_type = fu_chassis_kind["FU_SMBIOS_CHASSIS_KIND_DESKTOP"]
        else:
            chassis_type = fu_chassis_kind["FU_SMBIOS_CHASSIS_KIND_OTHER"]

        for fn in args.json_file_list:
            try:
                with open(fn, "rb") as f_in:
                    json_in = f_in.read().decode()
            except FileNotFoundError:
                json_in = _get_host_devices_and_attrs()
            json_out = _minimize_json(json_in, args.machine, chassis_type).encode()
            with open(fn, "wb") as f_out:
                f_out.write(json_out)


if __name__ == "__main__":
    main()
