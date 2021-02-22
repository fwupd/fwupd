Thelio IO Support
=================

Introduction
------------

This plugin is used to detach the Thelio IO device to DFU mode.

To switch to this mode `1` has to be written to the `bootloader` file
in sysfs.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_1209&PID_1776&REV_0001`

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

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1209`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
