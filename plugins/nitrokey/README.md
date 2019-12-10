Nitrokey Support
================

Introduction
------------

This plugin is used to get the correct version number on Nitrokey storage
devices. These devices have updatable firmware but so far no updates are
available from the vendor.

The device is switched to a DFU bootloader only when the secret firmware pin
is entered into the nitrokey-app tool. This cannot be automated.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_20A0&PID_4109&REV_0001`
 * `USB\VID_20A0&PID_4109`
 * `USB\VID_20A0`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x20A0`
in runtime mode and `USB:0x03EB` in bootloader mode.
