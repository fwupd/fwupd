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

Update Behavior
---------------

The device usually presents in runtime mode, but on detach re-enumerates with a
different USB VID and PID in DFU mode. The device is then handled by the `dfu`
plugin.

On DFU attach the device again re-enumerates back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x20A0`
in runtime mode and `USB:0x03EB` in bootloader mode.

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
