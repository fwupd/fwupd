#!/usr/bin/env python3
#
# Copyright 2024 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring

import json
import sys
import hashlib
import zipfile


def _minimize_usb_events(data) -> None:
    for device in data["UsbDevices"]:
        try:
            for event in device.get("UsbEvents", []) + device.get("Events", []):
                if event["Id"].startswith("#"):
                    continue
                event["Id"] = "#" + hashlib.sha1(event["Id"].encode()).hexdigest()[:8]
        except KeyError:
            pass


def _minimize_json(fn: str) -> None:
    with open(fn, "rb") as f:
        data = json.loads(f.read())
        _minimize_usb_events(data)
    with open(fn, "wb") as f:
        f.write(json.dumps(data, indent=2).encode())


def _minimize_zip(fn: str) -> None:
    files = {}
    with zipfile.ZipFile(fn) as myzip:
        for name in myzip.namelist():
            with myzip.open(name) as f:
                data = json.loads(f.read())
                _minimize_usb_events(data)
                files[name] = data
    fn_new = fn.replace(".zip", "-min.zip")
    with zipfile.ZipFile(
        fn_new, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as myzip:
        for name, data in files.items():
            myzip.writestr(name, json.dumps(data, separators=(",", ":")).encode())


if __name__ == "__main__":
    for fn in sys.argv[1:]:
        if fn.endswith(".json"):
            _minimize_json(fn)
        if fn.endswith(".zip"):
            _minimize_zip(fn)
