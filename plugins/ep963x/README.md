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

Update Behavior
---------------

The device usually presents in runtime mode, but on detach re-enumerates with
the same USB PID in an unlocked mode. On attach the device again re-enumerates
back to the runtime locked mode.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x17EF`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
