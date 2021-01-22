System76 Launch Support
=================

Introduction
------------

This plugin is used to detach the System76 Launch device to DFU mode.

To switch to bootloader mode a USB packet must be written, as specified by the
System76 EC protocol.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_3384&PID_0001&REV_0001`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x3384`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
