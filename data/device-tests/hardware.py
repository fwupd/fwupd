#!/usr/bin/python3
# pylint: disable=wrong-import-position,too-many-locals,unused-argument,wrong-import-order
#
# Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import gi
import os
import requests
import time
import sys
import glob
import json
from termcolor import colored

gi.require_version("Fwupd", "2.0")

from gi.repository import Fwupd
from gi.repository import Gio
from gi.repository import GLib


def _get_cache_file(fn):
    cachedir = os.path.expanduser("~/.cache/fwupdmgr")
    if not os.path.exists(cachedir):
        os.makedirs(cachedir)
    cachefn = os.path.join(cachedir, fn)
    if not os.path.exists(cachefn):
        url = "https://fwupd.org/downloads/" + fn
        print("Downloading", url)
        r = requests.get(url)
        r.raise_for_status()
        f = open(cachefn, "wb")
        f.write(r.content)
        f.close()
    return cachefn


class DeviceTest:
    def __init__(self, obj):
        self.client = Fwupd.Client.new()
        self.name = obj.get("name", "Unknown")
        self.guids = obj.get("guids", [])
        self.releases = obj.get("releases", [])
        self.has_runtime = obj.get("runtime", True)
        self.interactive = obj.get("interactive", False)
        self.disabled = obj.get("disabled", False)
        self.protocol = obj.get("protocol", None)

    def _info(self, msg):
        print(colored("[INFO]".ljust(10), "blue"), msg)

    def _warn(self, msg):
        print(colored("[WARN]".ljust(10), "yellow"), msg)

    def _failed(self, msg):
        print(colored("[FAILED]".ljust(10), "red"), msg)

    def _success(self, msg):
        print(colored("[SUCCESS]".ljust(10), "green"), msg)

    def _get_by_device_guids(self):
        cancellable = Gio.Cancellable.new()
        for d in self.client.get_devices(cancellable):
            for guid in self.guids:
                if d.has_guid(guid):
                    if self.protocol and self.protocol != d.get_protocol():
                        continue
                    return d
        return None

    def run(self):

        print("Running test on {}".format(self.name))
        dev = self._get_by_device_guids()
        if not dev:
            self._warn("no {} attached".format(self.name))
            return

        self._info("Current version {}".format(dev.get_version()))

        # apply each file
        for obj in self.releases:
            ver = obj.get("version")
            fn = obj.get("file")
            repeat = obj.get("repeat", 1)
            try:
                fn_cache = _get_cache_file(fn)
            except requests.exceptions.HTTPError as e:
                self._failed("Failed to download: {}".format(str(e)))
                return

            # some hardware updates more than one partition with the same firmware
            for cnt in range(0, repeat):
                if dev.get_version() == ver:
                    flags = Fwupd.InstallFlags.ALLOW_REINSTALL
                    self._info("Reinstalling version {}".format(ver))
                else:
                    flags = Fwupd.InstallFlags.ALLOW_OLDER
                    self._info("Installing version {}".format(ver))
                cancellable = Gio.Cancellable.new()

                try:
                    self.client.install(dev.get_id(), fn_cache, flags, cancellable)
                except GLib.Error as e:
                    if str(e).find("no HWIDs matched") != -1:
                        self._info("Skipping as {}".format(e))
                        continue
                    self._failed("Could not install: {}".format(e))
                    return

                # verify version
                if self.has_runtime:
                    dev = self._get_by_device_guids()
                    if not dev:
                        self._failed("Device did not come back: " + self.name)
                        return
                    if not dev.get_version():
                        self._failed("No version set after flash for: " + self.name)
                        return
                    if cnt == repeat - 1 and dev.get_version() != ver:
                        self._failed("Got: " + dev.get_version() + ", expected: " + ver)
                        return
                    self._success("Installed {}".format(dev.get_version()))
                else:
                    self._success("Assumed success (no runtime)")

            # wait for device to settle?
            time.sleep(2)


if __name__ == "__main__":

    # get manifests to parse
    device_fns = []
    if len(sys.argv) == 1:
        device_fns.extend(glob.glob("devices/*.json"))
    else:
        for fn in sys.argv[1:]:
            device_fns.append(fn)

    # run each test
    for fn in sorted(device_fns):
        print("{}:".format(fn))
        with open(fn, "r") as f:
            try:
                obj = json.load(f)
            except json.decoder.JSONDecodeError as e:
                print("Failed to parse {}: {}".format(fn, e))
                continue
        t = DeviceTest(obj)
        if t.disabled:
            continue
        if t.interactive and len(device_fns) > 1:
            continue
        t.run()
    sys.exit(0)
