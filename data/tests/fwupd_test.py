#!/usr/bin/env python3
#
# Copyright 2024 Mario Limonciello <mario.limonciello@amd.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

# to record a string for a device use
# umockdev-record $PATH

import os
import subprocess
import sys
import tempfile
import unittest
import dbusmock

import gi
from gi.repository import GLib
from gi.repository import Gio

gi.require_version("UMockdev", "1.0")
from gi.repository import UMockdev


class FwupdTest(dbusmock.DBusTestCase):
    DBUS_NAME = "org.freedesktop.fwupd"
    DBUS_PATH = "/"
    DBUS_INTERFACE = "org.freedesktop.fwupd"

    @classmethod
    def setUpClass(cls):
        if "DAEMON_BUILDDIR" in os.environ:
            libexecdir = os.environ["DAEMON_BUILDDIR"]
        else:
            libexecdir = "@LIBEXECDIR@"
        cls.daemon_path = os.path.join(libexecdir, "fwupd")
        for dir in ["STATE_DIRECTORY", "CACHE_DIRECTORY"]:
            if dir in os.environ:
                os.makedirs(os.environ[dir], exist_ok=True)
        os.environ["G_DEBUG"] = "fatal_warnings"
        GLib.log_set_always_fatal(
            GLib.LogLevelFlags.LEVEL_WARNING
            | GLib.LogLevelFlags.LEVEL_ERROR
            | GLib.LogLevelFlags.LEVEL_CRITICAL
        )
        # set up a fake system D-BUS
        cls.start_system_bus()
        cls.dbus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)

    def setUp(self):
        self.testbed = UMockdev.Testbed.new()
        self.polkitd, self.obj_polkit = self.spawn_server_template(
            "polkitd", {}, stdout=subprocess.PIPE
        )
        self.obj_polkit.SetAllowed(
            [
                "org.freedesktop.fwupd.update-internal",
                "org.freedesktop.fwupd.update-internal-trusted",
                "org.freedesktop.fwupd.downgrade-internal-trusted",
                "org.freedesktop.fwupd.downgrade-internal",
                "org.freedesktop.fwupd.update-hotplug-trusted",
                "org.freedesktop.fwupd.update-hotplug",
                "org.freedesktop.fwupd.downgrade-hotplug-trusted",
                "org.freedesktop.fwupd.downgrade-hotplug",
                "org.freedesktop.fwupd.device-unlock",
                "org.freedesktop.fwupd.modify-config",
                "org.freedesktop.fwupd.device-activate",
                "org.freedesktop.fwupd.verify-update",
                "org.freedesktop.fwupd.modify-remote",
                "org.freedesktop.fwupd.set-approved-firmware",
                "org.freedesktop.fwupd.self-sign",
                "org.freedesktop.fwupd.get-bios-settings",
                "org.freedesktop.fwupd.set-bios-settings",
                "org.freedesktop.fwupd.fix-host-security-attr",
                "org.freedesktop.fwupd.undo-host-security-attr",
            ]
        )

        self.proxy = None
        self.props_proxy = None
        self.daemon_log = None
        self.daemon = None
        self.changed_properties = {}

    def run(self, result=None):
        super().run(result)
        if not result or not self.daemon_log:
            return
        if len(result.errors) + len(result.failures) or os.getenv("VERBOSE"):
            with open(self.daemon_log.name, encoding="utf-8") as tmpf:
                sys.stderr.write("\nDAEMON log:\n")
                sys.stderr.write(tmpf.read())
                sys.stderr.write("\n")

    def tearDown(self):
        del self.testbed
        self.stop_daemon()

        if self.polkitd:
            self.polkitd.stdout.close()
            try:
                self.polkitd.kill()
            except OSError:
                pass
            self.polkitd.wait()

        self.obj_polkit = None

    #
    # Daemon control and D-BUS I/O
    #

    def start_daemon(self):
        """Start daemon and create DBus proxy.

        When done, this sets self.proxy as the Gio.DBusProxy for power-profiles-daemon.
        """
        env = os.environ.copy()
        env["G_DEBUG"] = "fatal-criticals"
        env["G_MESSAGES_DEBUG"] = "all"
        # note: Python doesn't propagate the setenv from Testbed.new(), so we
        # have to do that ourselves
        env["UMOCKDEV_DIR"] = self.testbed.get_root_dir()
        self.daemon_log = (
            tempfile.NamedTemporaryFile()
        )  # pylint: disable=consider-using-with
        daemon_path = [self.daemon_path, "-vv"]

        # pylint: disable=consider-using-with
        self.daemon = subprocess.Popen(
            daemon_path, env=env, stdout=self.daemon_log, stderr=subprocess.STDOUT
        )
        self.addCleanup(self.daemon.kill)

        def on_proxy_connected(_, res):
            try:
                self.proxy = Gio.DBusProxy.new_finish(res)
            except GLib.Error as exc:
                self.fail(exc)

        cancellable = Gio.Cancellable()
        self.addCleanup(cancellable.cancel)
        Gio.DBusProxy.new(
            self.dbus,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            None,
            self.DBUS_NAME,
            self.DBUS_PATH,
            self.DBUS_INTERFACE,
            cancellable,
            on_proxy_connected,
        )

        # wait until the daemon gets online
        wait_time = 60 if "valgrind" in daemon_path[0] else 5
        self.assert_eventually(
            lambda: self.proxy and self.proxy.get_name_owner(),
            timeout=wait_time * 1000,
            message=f"daemon did not start in {wait_time} seconds",
        )

        def properties_changed_cb(_, changed_properties, invalidated):
            self.changed_properties.update(changed_properties.unpack())

        self.addCleanup(
            self.proxy.disconnect,
            self.proxy.connect("g-properties-changed", properties_changed_cb),
        )

        self.assertEqual(self.daemon.poll(), None, "daemon crashed")

    def stop_daemon(self):
        """Stop the daemon if it is running."""

        if self.daemon:
            try:
                self.daemon.terminate()
            except OSError:
                pass
            self.assertEqual(self.daemon.wait(timeout=3000), 0)

        self.daemon = None
        self.proxy = None

    def assert_eventually(self, condition, message=None, timeout=5000):
        """Assert that condition function eventually returns True.

        Timeout is in milliseconds, defaulting to 5000 (5 seconds). message is
        printed on failure.
        """
        if condition():
            return

        done = False

        def on_timeout_reached():
            nonlocal done
            done = True

        source = GLib.timeout_add(timeout, on_timeout_reached)
        while not done:
            if condition():
                GLib.source_remove(source)
                return
            GLib.MainContext.default().iteration(False)

        self.fail(message or "timed out waiting for " + str(condition))

    def assert_dbus_property_eventually_is(self, prop, value, timeout=1200):
        """Asserts that a dbus property eventually is what expected"""
        return self.assert_eventually(
            lambda: self.get_dbus_property(prop) == value,
            timeout=timeout,
            message=f"property '{prop}' is not '{value}', but "
            + f"'{self.get_dbus_property(prop)}'",
        )

    def ensure_dbus_properties_proxies(self):
        self.props_proxy = Gio.DBusProxy.new_sync(
            self.dbus,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START
            | Gio.DBusProxyFlags.DO_NOT_AUTO_START_AT_CONSTRUCTION
            | Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES
            | Gio.DBusProxyFlags.DO_NOT_CONNECT_SIGNALS,
            None,
            self.DBUS_NAME,
            self.DBUS_PATH,
            "org.freedesktop.DBus.Properties",
            None,
        )

    def get_dbus_property(self, name):
        """Get property value from daemon D-Bus interface."""
        self.ensure_dbus_properties_proxies()
        return self.props_proxy.Get("(ss)", self.DBUS_NAME, name)

    def start_upower(self, on_battery):
        """Start the upower service with the given on_battery state"""

        sys_bat = self.testbed.add_device(
            "power_supply",
            "fakeBAT0",
            None,
            [
                "type",
                "Battery",
                "present",
                "1",
                "status",
                "Discharging",
                "energy_full",
                "60000000",
                "energy_full_design",
                "80000000",
                "energy_now",
                "48000000",
                "voltage_now",
                "12000000",
            ],
            ["POWER_SUPPLY_ONLINE", "1"],
        )

        self.upowerd, obj_upower = self.spawn_server_template(
            "upower",
            {"DaemonVersion": "0.99", "OnBattery": on_battery},
            stdout=subprocess.PIPE,
        )

    def stop_upower(self):
        """Stop the upower service"""
        self.upowerd.terminate()
        self.upowerd.wait()


class VeryBasicTest(FwupdTest):
    """Test the basic properties of the daemon."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()

        gi.require_version("Fwupd", "2.0")
        from gi.repository import Fwupd  # pylint: disable=wrong-import-position

        cls.client = Fwupd.Client()

    def test_properties(self):
        """Test the properties of the daemon without a client."""
        self.start_daemon()

        self.assertEqual(self.get_dbus_property("Interactive"), False)
        self.assertEqual(self.get_dbus_property("OnlyTrusted"), True)
        self.assertEqual(self.get_dbus_property("Tainted"), False)
        self.assertEqual(self.get_dbus_property("HostBkc"), "")
        self.assertEqual(self.get_dbus_property("HostProduct"), "Unknown Product")
        self.assertEqual(self.get_dbus_property("HostVendor"), "Unknown Vendor")
        self.assertEqual(self.get_dbus_property("Percentage"), 0)
        self.assertEqual(self.get_dbus_property("Status"), 1)
        self.assertEqual(self.get_dbus_property("BatteryLevel"), 101)

    def test_get_devices(self):
        """Test the libfwupd client get-devices interface."""
        self.start_daemon()

        devices = self.client.get_devices()
        # Should be at least the CPU test is running on
        self.assertGreater(len(devices), 0)


if __name__ == "__main__":
    # run ourselves under umockdev
    if "umockdev" not in os.environ.get("LD_PRELOAD", ""):
        os.execvp("umockdev-wrapper", ["umockdev-wrapper", sys.executable] + sys.argv)

    prog = unittest.main(exit=False)
    if prog.result.errors or prog.result.failures:
        sys.exit(1)

    # Translate to skip error
    if prog.result.testsRun == len(prog.result.skipped):
        sys.exit(77)
