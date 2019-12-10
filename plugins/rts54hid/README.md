Realtek RTS54HID HID Support
=========================

Introduction
------------

This plugin allows the user to update any supported hub and attached downstream
ICs using a custom HID-based flashing protocol. It does not support any RTS54xx
device using the HUB update protocol.

Other devices connected to the RTS54HIDxx using I2C will be supported soon.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

 * com.realtek.rts54

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_0BDA&PID_1100&REV_0001`
 * `USB\VID_0BDA&PID_1100`
 * `USB\VID_0BDA`

Child I²C devices are created using the device number as a suffix, for instance:

 * `USB\VID_0BDA&PID_1100&I2C_01`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0BDA`

Quirk use
---------
This plugin uses the following plugin-specific quirks:

| Quirk                  | Description                                 | Minimum fwupd version |
|------------------------|---------------------------------------------|-----------------------|
| `Rts54SlaveAddr`       | The slave address of a child module.        | 1.1.3                 |
| `Rts54I2cSpeed`        | The I2C speed to operate at (0, 1, 2).      | 1.1.3                 |
| `Rts54RegisterAddrLen` | The I2C register address length of commands | 1.1.3                 |
