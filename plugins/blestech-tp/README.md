---
title: Plugin: Betterlife Touch Controller Sensor
---

## Introduction

This plugin allows updating Touchpad devices from Betterlife. Devices are enumerated
using HID and raw I²C nodes.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `com.blestech.tp`

## GUID Generation

These device uses the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_347D&DEV_7953`

## Update Behavior

Firmware updates are initiated while the device is in its normal runtime mode.
Before writing the new image, the plugin switches the controller into a
dedicated boot/bootloader update mode where the firmware payload is
transferred. After the transfer completes, the device resets and returns to
normal runtime mode running the new firmware.

## Vendor ID Security

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x347D`

## External Interface Access

This plugin requires ioctl access to `HIDIOCSFEATURE` and `HIDIOCGFEATURE`.

## Version Considerations

This plugin has been available since fwupd version `2.1.1`.
