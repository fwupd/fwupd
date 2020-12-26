PixArt RF Devices Support
=========================

Introduction
------------

This plugin allows the user to update any supported Pixart RF Device using a
custom HID-based OTA protocol

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

 * com.pixart.rf

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `HIDRAW\VEN_093A&DEV_2801`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x093A`

External interface access
-------------------------
This plugin requires ioctl `HIDIOCSFEATURE` access.
