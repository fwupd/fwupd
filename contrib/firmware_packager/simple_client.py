#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+
"""A simple fwupd frontend"""
import sys
import os
import dbus
import gi
from gi.repository import GLib

gi.require_version("Fwupd", "2.0")
from gi.repository import Fwupd  # pylint: disable=wrong-import-position


class Progress:
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
                if i < percent / 2:
                    status_str += "*"
                else:
                    status_str += " "
            status_str += "] %d%% %s" % (percent, status)
            self.erase = len(status_str)
            sys.stdout.write(status_str)
            sys.stdout.flush()
            if "idle" in status:
                sys.stdout.write("\n")


def parse_args():
    """Parse arguments for this client"""
    import argparse

    parser = argparse.ArgumentParser(description="Interact with fwupd daemon")
    parser.add_argument(
        "--allow-older",
        action="store_true",
        help="Install older payloads(default False)",
    )
    parser.add_argument(
        "--allow-reinstall",
        action="store_true",
        help="Reinstall payloads(default False)",
    )
    parser.add_argument(
        "command",
        choices=[
            "get-devices",
            "get-details",
            "install",
            "refresh",
            "get-bios-setting",
        ],
        help="What to do",
    )
    parser.add_argument("cab", nargs="?", help="CAB file")
    parser.add_argument("deviceid", nargs="?", help="DeviceID to operate on(optional)")
    parser.add_argument("--setting", help="BIOS setting to operate on(optional)")
    args = parser.parse_args()
    return args


def refresh(client):
    """Uses fwupd client to refresh metadata"""
    remotes = client.get_remotes()
    client.set_user_agent_for_package("simple_client", "@FWUPD_VERSION@")
    for remote in remotes:
        if not remote.get_enabled():
            continue
        if remote.get_kind() != Fwupd.RemoteKind.DOWNLOAD:
            continue
        client.refresh_remote(remote)


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


def get_bios_settings(client, setting):
    """Use fwupd client to get BIOS settings"""
    settings = client.get_bios_settings()
    for i in settings:
        if not setting or setting == i.get_name() or setting == i.get_id():
            print(i.to_string())


def status_changed(client, spec, progress):  # pylint: disable=unused-argument
    """Signal emitted by fwupd daemon indicating status changed"""
    progress.status_changed(
        client.get_percentage(), Fwupd.status_to_string(client.get_status())
    )


def device_changed(client, device, progress):  # pylint: disable=unused-argument
    """Signal emitted by fwupd daemon indicating active device changed"""
    progress.device_changed(device.get_name())


def modify_config(client, key, value):
    """Use fwupd client to modify daemon configuration value"""
    try:
        print("setting configuration key %s to %s" % (key, value))
        client.modify_config(key, value, None)
    except Exception as e:
        print("%s" % str(e))
        sys.exit(1)


def install(client, cab, target, older, reinstall):
    """Use fwupd client to install CAB file to applicable devices"""
    # FWUPD_DEVICE_ID_ANY
    if not target:
        target = "*"
    flags = Fwupd.InstallFlags.NONE
    if older:
        flags |= Fwupd.InstallFlags.ALLOW_OLDER
    if reinstall:
        flags |= Fwupd.InstallFlags.ALLOW_REINSTALL
    progress = Progress()
    parent = super(client.__class__, client)
    parent.connect("device-changed", device_changed, progress)
    parent.connect("notify::percentage", status_changed, progress)
    parent.connect("notify::status", status_changed, progress)
    try:
        client.install(target, cab, flags, None)
    except GLib.Error as glib_err:  # pylint: disable=catching-non-exception
        progress.status_changed(0, "idle")
        print("%s" % glib_err)
        sys.exit(1)
    print("\n")


def get_daemon_property(key: str):
    try:
        bus = dbus.SystemBus()
        proxy = bus.get_object(bus_name="org.freedesktop.fwupd", object_path="/")
        iface = dbus.Interface(proxy, "org.freedesktop.DBus.Properties")
        val = iface.Get("org.freedesktop.fwupd", key)
        if isinstance(val, dbus.Boolean):
            print(
                "org.freedesktop.fwupd property %s, current value is %s"
                % (key, bool(val))
            )
        else:
            print("org.freedesktop.fwupd property %s, current value is %s" % (key, val))
        return val
    except dbus.DBusException as e:
        print(e)
    return None


def check_exists(cab):
    """Check that CAB file exists"""
    if not cab:
        print("Need to specify payload")
        sys.exit(1)
    if not os.path.isfile(cab):
        print("%s doesn't exist or isn't a file" % cab)
        sys.exit(1)


if __name__ == "__main__":
    ARGS = parse_args()
    CLIENT = Fwupd.Client()

    if ARGS.command == "get-devices":
        get_devices(CLIENT)
    elif ARGS.command == "get-details":
        check_exists(ARGS.cab)
        get_details(CLIENT, ARGS.cab)
    elif ARGS.command == "refresh":
        refresh(CLIENT)
    elif ARGS.command == "install":
        check_exists(ARGS.cab)
        install(CLIENT, ARGS.cab, ARGS.deviceid, ARGS.allow_older, ARGS.allow_reinstall)
    elif ARGS.command == "get-bios-setting":
        get_bios_settings(CLIENT, ARGS.setting)
