Parade LSPCON
=============

Introduction
------------

This plugin uses `flashrom` to update the firmware for DisplayPort-to-HDMI
converters (LSPCONs) made by Parade Technologies.

Firmware Format
---------------

Device firmware images are blobs in an unspecified format, which are programmed
directly to a chosen block of the device Flash.

This plugin supports the following protocol ID:

 * com.paradetech.ps175

GUID Generation
---------------

These devices use custom DeviceInstanceIds:

 * `PARADE-LSPCON\NAME_1AF80175:00`: derived from the name of the I2C
   device in sysfs, itself derived from values provided by system firmware.
 * `FLASHROM-LSPCON-I2C-SPI\VEN_1AF8&DEV_0175`: vendor and device
   ID taken from sysfs name; compatible with previous device support
   in the `flashrom` plugin.

Update Behavior
---------------

The firmware is deployed to the SPI chip when the machine is in normal runtime
mode, but it is only used when the device is rebooted.

Vendor ID Security
------------------

The vendor ID is hard-coded to Parade, `PCI:0x1AF8`.


External interface access
---
This plugin requires read and write access to the I2C bus the device is connected
to, like `/dev/i2c-7`.
