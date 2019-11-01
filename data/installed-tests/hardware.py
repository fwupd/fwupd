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

gi.require_version('Fwupd', '2.0')

from gi.repository import Fwupd
from gi.repository import Gio
from gi.repository import GLib

def _get_by_device_guid(client, guid):
    cancellable = Gio.Cancellable.new()
    devices = client.get_devices(cancellable)
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
        url = 'https://fwupd.org/downloads/' +  fn
        print("Downloading", url)
        r = requests.get(url)
        r.raise_for_status()
        f = open(cachefn, 'wb')
        f.write(r.content)
        f.close()
    return cachefn

class Test:
    def __init__(self, name, guid, has_runtime=True):
        self.files = []
        self.name = name
        self.guid = guid
        self.has_runtime = has_runtime

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
            if self.has_runtime:
                dev = _get_by_device_guid(client, self.guid)
                if not dev:
                    raise GLib.Error('Device did not come back: ' + self.name)
                if not dev.get_version():
                    raise GLib.Error('No version set after flash for: ' + self.name)
                if dev.get_version() != ver:
                    raise GLib.Error('Got: ' + dev.get_version() + ', expected: ' + ver)

            # FIXME: wait for device to settle?
            time.sleep(2)

    def add_file(self, fn, ver):
        self.files.append((fn, ver))

if __name__ == '__main__':

    tests = []

    # DFU A3BU XPLAINED Mouse
    test = Test('DfuXmegaA3BU-Xplained', '80478b9a-3643-5e47-ab0f-ed28abe1019d')
    test.add_file('f5bbeaba1037dce31dd12f349e8148ae35f98b61-a3bu-xplained123.cab', '1.23')
    test.add_file('24d838541efe0340bf67e1cc5a9b95526e4d3702-a3bu-xplained124.cab', '1.24')
    tests.append(test)

    # DFU AT90USBKEY Mouse
    test = Test('DfuAT90USBKEY', 'c1874c52-5f6a-5864-926d-ea84bcdc82ea')
    test.add_file('b6bef375597e848971f230cf992c9740f7bf5b92-at90usbkey123.cab', '1.23')
    test.add_file('47807fd4a94a4d5514ac6bf7a73038e00ed63225-at90usbkey124.cab', '1.24')
    tests.append(test)

    # Logitech K780 Keyboard
    test = Test('LogitechMPK01', '3932ba15-2bbe-5bbb-817e-6c74e7088509')
    test.add_file('d81a81e13952e871ca2eb86cba7e66199e576a38-Logitech-K780-MPK01.02_B0021.cab', 'MPK01.02_B0021')
    test.add_file('b0dffe84c6d3681e7ae5f27509781bc1cf924dd7-Logitech-K780-MPK01.03_B0024.cab', 'MPK01.03_B0024')
    tests.append(test)

    # Hughski ColorHug (a special variant) using 'dfu'
    test = Test('ColorHugDFU', 'dfbaaded-754b-5214-a5f2-46aa3331e8ce')
    test.add_file('77b315dcaa7edc1d5fbb77016b94d8a0c0133838-fakedevice01_dfu.cab', '0.1')
    test.add_file('8bc3afd07a0af3baaab8b19893791dd3972e8305-fakedevice02_dfu.cab', '0.2')
    tests.append(test)

    # Hughski ColorHug2 using 'colorhug'
    test = Test('ColorHug2', '2082b5e0-7a64-478a-b1b2-e3404fab6dad')
    test.add_file('170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab', '2.0.6')
    test.add_file('0a29848de74d26348bc5a6e24fc9f03778eddf0e-hughski-colorhug2-2.0.7.cab', '2.0.7')
    tests.append(test)

    # Logitech Unifying Receiver (RQR12) using logitech_hidpp
    test = Test('UnifyingRQR12', '9d131a0c-a606-580f-8eda-80587250b8d6')
    test.add_file('6e5ab5961ec4c577bff198ebb465106e979cf686-Logitech-Unifying-RQR12.05_B0028.cab', 'RQR12.05_B0028')
    test.add_file('938fec082652c603a1cdafde7cd25d76baadc70d-Logitech-Unifying-RQR12.07_B0029.cab', 'RQR12.07_B0029')
    tests.append(test)

    # Logitech Unifying Receiver (RQR24) using logitech_hidpp
    test = Test('UnifyingRQR24', 'cc4cbfa9-bf9d-540b-b92b-172ce31013c1')
    test.add_file('82b90b2614a9a4d0aced1ab8a4a99e228c95585c-Logitech-Unifying-RQ024.03_B0027.cab', 'RQR24.03_B0027')
    test.add_file('4511b9b0d123bdbe8a2007233318ab215a59dfe6-Logitech-Unifying-RQR24.05_B0029.cab', 'RQR24.05_B0029')
    tests.append(test)

    # Jabra Speak 510
    test = Test('JabraSpeak510', '443b9b32-7603-5c3a-bb30-291a7d8d6dbd')
    test.add_file('45f88c50e79cfd30b6599df463463578d52f2fe9-Jabra-SPEAK_510-2.10.cab', '2.10')
    test.add_file('c0523a98ef72508b5c7ddd687418b915ad5f4eb9-Jabra-SPEAK_510-2.14.cab', '2.14')
    tests.append(test)

    # Jabra Speak 410
    test = Test('JabraSpeak410', '1764c519-4723-5514-baf9-3b42970de487')
    test.add_file('eab97d7e745e372e435dbd76404c3929730ac082-Jabra-SPEAK_410-1.8.cab', '1.8')
    test.add_file('50a03efc5df333a948e159854ea40e1a3786c34c-Jabra-SPEAK_410-1.11.cab', '1.11')
    tests.append(test)

    # Jabra Speak 710
    test = Test('JabraSpeak710', '0c503ad9-4969-5668-81e5-a3748682fc16')
    test.add_file('d2910cdbc45cf172767d05e60d9e39a07a10d242-Jabra-SPEAK_710-1.10.cab', '1.10')
    test.add_file('a5c627ae42de4e5c3ae3df28977f480624f96f66-Jabra-SPEAK_710-1.28.cab', '1.28')
    tests.append(test)

    # 8Bitdo SFC30 Gamepad
    test = Test('8BitdoSFC30', 'a7fcfbaf-e9e8-59f4-920d-7691dc6c8699')
    test.add_file('fe066b57c69265f4cce8a999a5f8ab90d1c13b24-8Bitdo-SFC30_NES30_SFC30_SNES30-4.01.cab', '4.01')
    tests.append(test)

    # 8Bitdo NES30Pro Gamepad
    test = Test('8BitdoNES30Pro', 'c6566b1b-0c6e-5d2e-9376-78c23ab57bf2')
    test.add_file('1cb9a0277f536ecd81ca1cea6fd80d60cdbbdcd8-8Bitdo-SFC30PRO_NES30PRO-4.01.cab', '4.01')
    tests.append(test)

    # 8Bitdo SF30 Pro Gamepad
    test = Test('8BitdoSF30Pro', '269b3121-097b-50d8-b9ba-d1f64f9cd241')
    test.add_file('3d3a65ee2e8581647fb09d752fa7e21ee1566481-8Bitdo-SF30_Pro-SN30_Pro-1.26.cab', '1.26')
    tests.append(test)

    # AIAIAI H05
    test = Test('AIAIAI-H05', '7e8318e1-27ae-55e4-a7a7-a35eff60e9bf', has_runtime=False)
    test.add_file('84279d6bab52262080531acac701523604f3e649-AIAIAI-H05-1.6.cab', '1.6')
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
