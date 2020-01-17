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

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1D5C`
