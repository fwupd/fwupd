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

These devices use the standard HIDRAW DeviceInstanceId values for Pixart Imaging, Inc, e.g.

 * `HIDRAW\VEN_093A`
 
Customer support HIDRAW DeviceInstanceId values for Primax Electronics, Ltd, e.g.

 * `HIDRAW\VEN_0461&DEV_4EEF`
 * `HIDRAW\VEN_0461&DEV_4EEE`
 * `HIDRAW\VEN_0461&DEV_4EF4`
 * `HIDRAW\VEN_0461&DEV_4EFF`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x093A`

External interface access
-------------------------
This plugin requires ioctl `HIDIOCSFEATURE` and `HIDIOCGFEATURE` access.
