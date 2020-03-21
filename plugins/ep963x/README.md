Explore EP963x Support
======================

Introduction
------------

The EP963x is a generic MCU used in many different products.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

 * tw.com.exploretech.ep963x

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_17EF&PID_7226&REV_0001`
 * `USB\VID_17EF&PID_7226`
 * `USB\VID_17EF`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x17EF`
