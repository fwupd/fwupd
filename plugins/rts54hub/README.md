Realtek RTS54 HUB Support
=========================

Introduction
------------

This plugin allows the user to update any supported hub and attached downstream
ICs using a custom HUB-based flashing protocol. It does not support any RTS54xx
device using the HID update protocol.

Other devices connected to the RTS54xx using I2C will be supported soon.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

 * com.realtek.rts54

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_0BDA&PID_5423&REV_0001`
 * `USB\VID_0BDA&PID_5423`
 * `USB\VID_0BDA`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0BDA`
