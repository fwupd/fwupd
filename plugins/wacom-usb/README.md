Wacom USB Support
=================

Introduction
------------

Wacom provides interactive pen displays, pen tablets, and styluses to equip and
inspire everyone make the world a more creative place.

From 2016 Wacom has been using a HID-based proprietary flashing algorithm which
has been documented by support team at Wacom and provided under NDA under the
understanding it would be used to build a plugin under a LGPLv2+ license.

Wacom devices are actually composite devices, with the main ARM CPU being
programmed using a more complicated erase, write, verify algorithm based
on a historical update protocol. The "sub-module" devices use a newer protocol,
again based on HID, but are handled differently depending on their type.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
the following formats:

 * Touch module: Intel HEX file format
 * Bluetooth module: Unknown airoflash file format
 * EMR module: Plain SREC file format
 * Main module: SREC file format, with a custom `WACOM` vendor header

This plugin supports the following protocol ID:

 * com.wacom.usb

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_056A&PID_0378&REV_0001`
 * `USB\VID_056A&PID_0378`
 * `USB\VID_056A`

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example set to `USB:0x056A`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
