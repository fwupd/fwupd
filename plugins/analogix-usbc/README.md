Analogix Device Support
==============

Introduction
------------

This plugin can flash the firmware of Analogix USBC Device.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format. The binary file has a vendor-specific header
that is used when flashing the image.

This plugin supports the following protocol ID:

 * com.analogix.bb

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_1F29&PID_7518`


Vendor ID Security
------------------

The vendor ID is set from the USB vendor. The list of USB VIDs used is:

 * `USB:0x1F29`


External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.