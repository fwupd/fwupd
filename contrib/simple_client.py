#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+
"""A simple fwupd frontend"""
import sys
import os
import gi
from gi.repository import GLib
gi.require_version('Fwupd', '2.0')
from gi.repository import Fwupd #pylint: disable=wrong-import-position

class Progress():
    """Class to track the signal changes of progress events"""
    def __init__(self):
        self.device = None
        self.status = None
        self.percent = 0
        self.erase = 0

    def device_changed(self, new_device):
        """Indicate new device string to track"""
        if self.device != new_device:
            self.device = new_device
            print("\nUpdating %s" % self.device)

    def status_changed(self, percent, status):
        """Indicate new status string or % complete to track"""
        if self.status != status or self.percent != percent:
            for i in range(0, self.erase):
                sys.stdout.write("\b \b")
            self.status = status
            self.percent = percent
            status_str = "["
            for i in range(0, 50):
                if i < percent/2:
                    status_str += '*'
                else:
                    status_str += ' '
            status_str += "] %d%% %s" %(percent, status)
            self.erase = len(status_str)
            sys.stdout.write(status_str)
            sys.stdout.flush()
            if 'idle' in status:
                sys.stdout.write("\n")

def parse_args():
    """Parse arguments for this client"""
    import argparse
    parser = argparse.ArgumentParser(description="Interact with fwupd daemon")
    parser.add_argument("--allow-older", action="store_true",
                        help="Install older payloads(default False)")
    parser.add_argument("--allow-reinstall", action="store_true",
                        help="Reinstall payloads(default False)")
    parser.add_argument("command", choices=["get-devices",
                                            "get-details",
                                            "install"], help="What to do")
    parser.add_argument('cab', nargs='?', help='CAB file')
    parser.add_argument('deviceid', nargs='?',
                        help='DeviceID to operate on(optional)')
    args = parser.parse_args()
    return args

def get_devices(client):
    """Use fwupd client to fetch devices"""
    devices = client.get_devices()
    for item in devices:
        print(item.to_string())

def get_details(client, cab):
    """Use fwupd client to fetch details for a CAB file"""
    devices = client.get_details(cab, None)
    for device in devices:
        print(device.to_string())

def status_changed(client, spec, progress): #pylint: disable=unused-argument
    """Signal emitted by fwupd daemon indicating status changed"""
    progress.status_changed(client.get_percentage(),
                            Fwupd.status_to_string(client.get_status()))

def device_changed(client, device, progress): #pylint: disable=unused-argument
    """Signal emitted by fwupd daemon indicating active device changed"""
    progress.device_changed(device.get_name())

def install(client, cab, target, older, reinstall):
    """Use fwupd client to install CAB file to applicable devices"""
    # FWUPD_DEVICE_ID_ANY
    if not target:
        target = '*'
    flags = Fwupd.InstallFlags.NONE
    if older:
        flags |= Fwupd.InstallFlags.ALLOW_OLDER
    if reinstall:
        flags |= Fwupd.InstallFlags.ALLOW_REINSTALL
    progress = Progress()
    parent = super(client.__class__, client)
    parent.connect('device-changed', device_changed, progress)
    parent.connect('notify::percentage', status_changed, progress)
    parent.connect('notify::status', status_changed, progress)
    try:
        client.install(target, cab, flags, None)
    except GLib.Error as glib_err: #pylint: disable=catching-non-exception
        progress.status_changed(0, 'idle')
        print("%s" % glib_err)
        sys.exit(1)
    print("\n")

def check_exists(cab):
    """Check that CAB file exists"""
    if not cab:
        print("Need to specify payload")
        sys.exit(1)
    if not os.path.isfile(cab):
        print("%s doesn't exist or isn't a file" % cab)
        sys.exit(1)

if __name__ == '__main__':
    ARGS = parse_args()
    CLIENT = Fwupd.Client()
    CLIENT.connect()

    if ARGS.command == "get-devices":
        get_devices(CLIENT)
    elif ARGS.command == "get-details":
        check_exists(ARGS.cab)
        get_details(CLIENT, ARGS.cab)
    elif ARGS.command == "install":
        check_exists(ARGS.cab)
        install(CLIENT, ARGS.cab, ARGS.deviceid, ARGS.allow_older, ARGS.allow_reinstall)
