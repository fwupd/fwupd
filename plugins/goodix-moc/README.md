Goodix Fingerprint Sensor Support
=================================

Introduction
------------

The plugin used for update firmware for fingerprint sensors from Goodix.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

 * com.goodix.goodixmoc

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_27C6&PID_6001&REV_0001`
 * `USB\VID_27C6&PID_6001`
 * `USB\VID_27C6`

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x27C6`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
