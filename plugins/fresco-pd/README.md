Fresco PD Support
=================

Introduction
------------

This plugin is used to update Power Devlivery devices by Fresco.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecifed binary format.

This plugin supports the following protocol ID:

 * com.frescologic.pd

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_1D5C&PID_7102&REV_0001`
 * `USB\VID_1D5C&PID_7102`
 * `USB\VID_1D5C`

These devices also use custom GUID values, e.g.

 * `USB\VID_1D5C&PID_7102&CID_01`

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1D5C`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
