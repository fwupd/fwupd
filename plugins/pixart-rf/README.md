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

These devices use the standard HIDRAW DeviceInstanceId values for both
Pixart Imaging, Inc and Primax Electronics, Ltd, e.g.

 * `HIDRAW\VEN_093A&DEV_2801`
 * `HIDRAW\VEN_0461&DEV_4EEF`
 * `HIDRAW\VEN_0461&DEV_4EEF&NAME_${NAME}`

Additionaly, a custom GUID values including the name is used, e.g.

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x093A`

External interface access
-------------------------
This plugin requires ioctl `HIDIOCSFEATURE` and `HIDIOCGFEATURE` access.
