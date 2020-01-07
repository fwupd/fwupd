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

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0603`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
