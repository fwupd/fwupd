#!/usr/bin/python3
#
# Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string

import json
import sys

from typing import Dict, List, Any

import gi
from gi.repository import GLib

gi.require_version("Fwupd", "2.0")
gi.require_version("Json", "1.0")

from gi.repository import Fwupd  # pylint: disable=wrong-import-position
from gi.repository import Json  # pylint: disable=wrong-import-position


def _minimize_json(json_str: str) -> str:
    nodes = json.loads(json_str)
    new_attrs: List[Dict[str, Any]] = []
    new_devices: List[Dict[str, Any]] = []
    new_bios_settings: List[Dict[str, Any]] = []
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
        for device in nodes["BiosSettings"]:
            new_attr: Dict[str, Any] = {}
            for key in device:
                if key not in ["Filename"]:
                    new_attr[key] = device[key]
            new_bios_settings.append(new_attr)
    except KeyError:
        pass
    return json.dumps(
        {
            "SecurityAttributes": new_attrs,
            "Devices": new_devices,
            "BiosSettings": new_bios_settings,
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

    # add BIOS settings
    try:
        attrs = client.get_bios_settings()
    except GLib.GError as e:
        print("ignoring {}".format(e))
    else:
        builder.set_member_name("BiosSettings")
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


if len(sys.argv) < 2:
    sys.stdout.write(_minimize_json(sys.stdin.read()))
else:
    for fn in sys.argv[1:]:

        try:
            with open(fn, "rb") as f_in:
                json_in = f_in.read().decode()
        except FileNotFoundError:
            json_in = _get_host_devices_and_attrs()
        json_out = _minimize_json(json_in).encode()
        with open(fn, "wb") as f_out:
            f_out.write(json_out)
