ColorHug Support
================

Introduction
------------

The ColorHug is an affordable open source display colorimeter built by
Hughski Limited. The USB device allows you to calibrate your screen for
accurate color matching.

ColorHug versions 1 and 2 support a custom HID-based flashing protocol, but
version 3 (ColorHug+) has now switched to DFU.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_273F&PID_1001&REV_0001`
 * `USB\VID_273F&PID_1001`
 * `USB\VID_273F`
