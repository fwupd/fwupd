---
title: Plugin: Elan Touchscreen
---

## Introduction

This plugin allows updating Elan touchscreen devices.

Devices are enumerated and accessed via the hidraw interface using the HID-over-I2C protocol.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in an unspecified
binary file format.

This plugin supports the following protocol ID:

* `tw.com.emc.elants`

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_04F3&DEV_2F6E`

## Update Behavior

The touchscreen device typically operates in normal mode.

During firmware updates, it transitions to IAP (bootloader) mode, where the touchscreen remains
non-functional until the process is complete.
Once the firmware write is finished, the device resets to exit IAP mode and runs the new firmware.

If a flash failure occurs, the device will remain non-functional while trapped in IAP mode,
but it can be recovered by performing a successful firmware write.

## Vendor ID Security

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x04F3`

## External Interface Access

This plugin requires read/write access to `/dev/hidraw`.

## Version Considerations

This plugin has been available since fwupd version `2.1.5`.
