PS175 I2C SPI Support
=================

Introduction
------------

PS175 DisplayPort to HDMI 2.0 protocol converter and HDMI 1.4b level shifter
provides support for display with HDMI.

The device is connected and communicated with I2c bus and the firmware update
is done by invoking flashrom command line executable. That requires extra
files including memory layout to help flash and the firmware blob has to be
a tar file.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob as
a tar file including the following files:

 * Flashrom layout file: Text file format
 * User flag 1: Binary file format
 * User flag 2: Binary file format
 * Main firmware blob: Binary file format

This plugin supports the following protocol ID:

 * com.parade.ps175

Quirk use
---------
This plugin uses the following plugin-specific quirks:

| Quirk                   | Description                         | Minimum fwupd version |
|-------------------------|-------------------------------------|-----------------------|
| `Programmer`            | Programmer name for flashrom        | 1.2.4                 |
| `Device`                | Device name                         | 1.2.4                 |
| `Protocol`              | Device protocol ID                  | 1.2.4                 |
| `VendorName`            | Device vendor name                  | 1.2.4                 |

GUID Generation
---------------

These devices use the customized Flashrom I2c DeviceInstanceId values, e.g.

 * `FLASHROM-I2C\VEN_1AF8&amp;DEV_0175

Vendor ID Security
------------------

The vendor ID is set from the I2C vendor, for example set to I2C:1AF8
