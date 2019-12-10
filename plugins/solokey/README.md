SoloKey Support
===============

Introduction
------------

The SoloKey Secure and Hacker is an affordable open source FIDO2 security key.

All hardware supports the U2F HID flashing protocol. The Hacker version is not
supported and the existing DFU update procedure should be used.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a JSON wrapped, base-64, Intel hex file.

This plugin supports the following protocol ID:

 * com.solokeys

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_0483&PID_A2CA&REV_0001`
 * `USB\VID_0483&PID_A2CA`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0483`
