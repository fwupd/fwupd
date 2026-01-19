---
title: Plugin: Focal TouchPad
---

## Introduction

This plugin allows updating Touchpad devices from Focal. Devices are enumerated
using HID . The I²C mode is used for firmware recovery.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `tw.com.focalfp`

## GUID Generation

These device uses the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_2808&DEV_0106`

## Update Behavior

The device usually presents in HID mode, and the firmware is written to the
device by switching to a IAP mode where the touchpad is nonfunctional.
Once complete the device is reset to get out of IAP mode and to load the new
firmware version.

On flash failure the device is nonfunctional, but is recoverable by writing
to the i2c device. This is typically much slower than updating the device
using HID and also requires a model-specific HWID quirk to match.

## Vendor ID Security

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x17EF`

## Quirk Use

This plugin uses the following plugin-specific quirks:

## External Interface Access

This plugin requires ioctl access to `HIDIOCSFEATURE` and `HIDIOCGFEATURE`.

## Version Considerations

This plugin has been available since fwupd version `1.8.6`.
