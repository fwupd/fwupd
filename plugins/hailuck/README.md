Hailuck Support
===============

Introduction
------------

Hailuck produce the firmware used on the keyboard and trackpad used in the
Pinebook Pro laptops.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

 * com.hailuck.kbd
 * com.hailuck.tp

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_0603&PID_1020`

Update Behavior
---------------

The keyboard device usually presents in runtime mode, but on detach it
re-enumerates with a different USB VID and PID in bootloader mode. On attach
the device again re-enumerates back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

The touchpad firmware is deployed when the device is in normal runtime mode,
and the device will reset when the new firmware has been written.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0603`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
