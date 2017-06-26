#!/usr/bin/python3
# pylint: disable=wrong-import-position,too-many-locals,unused-argument,wrong-import-order
#
# Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2

import gi
import os
import requests
import time

gi.require_version('Fwupd', '1.0')

from gi.repository import Fwupd
from gi.repository import Gio
from gi.repository import GLib

def _get_by_device_guid(client, guid):
    cancellable = Gio.Cancellable.new()
    devices = client.get_devices_simple(cancellable)
    for d in devices:
        if d.has_guid(guid):
            return d
    return None

def _get_cache_file(fn):
    cachedir = os.path.expanduser('~/.cache/fwupdmgr')
    if not os.path.exists(cachedir):
        os.makedirs(cachedir)
    cachefn = os.path.join(cachedir, fn)
    if not os.path.exists(cachefn):
        url = 'https://secure-lvfs.rhcloud.com/downloads/' +  fn
        print("Downloading", url)
        r = requests.get(url)
        r.raise_for_status()
        f = open(cachefn, 'wb')
        f.write(r.content)
        f.close()
    return cachefn

class Test:
    def __init__(self, name, guid):
        self.files = []
        self.name = name
        self.guid = guid

    def run(self):

        # connect to fwupd
        client = Fwupd.Client.new()
        dev = _get_by_device_guid(client, self.guid)
        if not dev:
            print("Skipping hardware test, no", self.name, "attached")
            return

        print(dev.get_name(), "is currently version", dev.get_version())

        # apply each file
        for fn, ver in self.files:
            fn_cache = _get_cache_file(fn)
            if dev.get_version() == ver:
                flags = Fwupd.InstallFlags.ALLOW_REINSTALL
            else:
                flags = Fwupd.InstallFlags.ALLOW_OLDER
            cancellable = Gio.Cancellable.new()
            print("Installing", fn_cache)
            client.install(dev.get_id(), fn_cache, flags, cancellable)

            # verify version
            dev = _get_by_device_guid(client, self.guid)
            if not dev:
                raise GLib.Error('Device did not come back: ' + name)
            if not dev.get_version():
                raise GLib.Error('No version set after flash for: ' + name)
            if dev.get_version() != ver:
                raise GLib.Error('Got: ' + dev.get_version() + ', expected: ' + ver)

            # FIXME: wait for device to settle?
            time.sleep(2)

    def add_file(self, fn, ver):
        self.files.append((fn, ver))

if __name__ == '__main__':

    tests = []

    # Hughski ColorHug (a special variant) using 'dfu'
    test = Test('ColorHugDFU', 'dfbaaded-754b-5214-a5f2-46aa3331e8ce')
    test.add_file('77b315dcaa7edc1d5fbb77016b94d8a0c0133838-fakedevice01_dfu.cab', '0.1')
    test.add_file('8bc3afd07a0af3baaab8b19893791dd3972e8305-fakedevice02_dfu.cab', '0.2')
    tests.append(test)

    # Hughski ColorHugALS using 'colorhug'
    test = Test('ColorHugALS', '84f40464-9272-4ef7-9399-cd95f12da696')
    test.add_file('73ac1aa98130e532c727308cc6560783b10ca3a9-hughski-colorhug-als-4.0.2.cab', '4.0.2')
    test.add_file('8dbdd54c712b33f72d866ce3b23b3ceed3ad494d-hughski-colorhug-als-4.0.3.cab', '4.0.3')
    tests.append(test)

    # Logitech Unifying Receiver (RQR12) using 'unifying'
    test = Test('UnifyingRQR12', '9d131a0c-a606-580f-8eda-80587250b8d6')
    test.add_file('6e5ab5961ec4c577bff198ebb465106e979cf686-Logitech-Unifying-RQR12.05_B0028.cab', 'RQR12.05_B0028')
    test.add_file('938fec082652c603a1cdafde7cd25d76baadc70d-Logitech-Unifying-RQR12.07_B0029.cab', 'RQR12.07_B0029')
    tests.append(test)

    # Logitech Unifying Receiver (RQR24) using 'unifying'
    test = Test('UnifyingRQR24', 'cc4cbfa9-bf9d-540b-b92b-172ce31013c1')
    test.add_file('82b90b2614a9a4d0aced1ab8a4a99e228c95585c-Logitech-Unifying-RQ024.03_B0027.cab', 'RQR24.03_B0027')
    test.add_file('4511b9b0d123bdbe8a2007233318ab215a59dfe6-Logitech-Unifying-RQR24.05_B0029.cab', 'RQR24.05_B0029')
    tests.append(test)

    # Logitech K780 Keyboard
    test = Test('LogitechMPK01', '3932ba15-2bbe-5bbb-817e-6c74e7088509')
    test.add_file('Logitech-K780-MPK01.03_B0024.cab', 'MPK01.03_B0024')
    tests.append(test)

    # run each test
    rc = 0
    for test in tests:
        try:
            test.run()
        except GLib.Error as e:
            print(str(e))
            rc = 1
        except requests.exceptions.HTTPError as e:
            print(str(e))
            rc = 1
