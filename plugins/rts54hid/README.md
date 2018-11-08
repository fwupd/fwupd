Realtek RTS54HID HID Support
=========================

Introduction
------------

This plugin allows the user to update any supported hub and attached downstream
ICs using a custom HID-based flashing protocol. It does not support any RTS54xx
device using the HUB update protocol.

Other devices connected to the RTS54HIDxx using I2C will be supported soon.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_0BDA&PID_1100&REV_0001`
 * `USB\VID_0BDA&PID_1100`
 * `USB\VID_0BDA`

Child I²C devices are created using the device number as a suffix, for instance:

 * `USB\VID_0BDA&PID_1100&I2C_01`

