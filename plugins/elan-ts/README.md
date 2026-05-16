---
title: Plugin: Elan Touchscreen
---

## Introduction

This plugin allows updating Elan touchscreen devices. 
Devices are enumerated and accessed via the hidraw interface 
using the HID-over-I2C protocol.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `tw.com.emc.elants`

## GUID Generation

These device uses the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_04F3&DEV_2F6E`

## Update Behavior

The touchscreen device typically operates in normal mode.
During firmware updates, it transitions to IAP (bootloader) mode, 
where the touchscreen remains non-functional until the process is complete.
Once the write is finished, the device resets to exit IAP mode and load the new firmware.

Firmware is written to the device by switching to a IAP (bootloader) mode,
where the touchscreen is nonfunctional.
Once complete the device is reset to get out of IAP mode and to load the new
firmware.

If a flash failure occurs, the device will remain nonfunctional while in IAP mode, 
but can be recovered by performing a successful firmware write.


## Update Behavior

The touchscreen device typically operates in normal mode. 
During firmware updates, it transitions to IAP (bootloader) mode, 
where the touchscreen remains non-functional. 
Once the write is complete, the device resets to exit IAP mode and load the new firmware.

If a flash failure occurs, the device will remain non-functional while trapped in IAP mode, 
but it can be recovered by performing a successful firmware write.

## Vendor ID Security

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x04F3`

## External Interface Access

This plugin requires read/write access to `/dev/hidraw`.

## Version Considerations

This plugin has been available since fwupd version `2.0.16`.
