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

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1209`
