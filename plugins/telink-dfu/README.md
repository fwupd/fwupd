---
title: Plugin: Telink DFU
---

## Introduction

This plugin allows upgrade of Telink OTA-compliant devices.

Supported devices:

* 8208 Dual Keyboard

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in a packed binary file format.

The cabinet file prepared by Telink Semiconductor includes a validated firmware image `firmware.bin`, an accompanying `manifest.json` of firmware description. At the moment only the version field takes effect.

This plugin supports the following protocol ID:

* `com.telink.dfu`

## GUID Generation

These devices use the standard BLUETOOTH DeviceInstanceId values, e.g.

* `BLUETOOTH\VID_248A&PID_8208`

HIDRAW is expected to be supported in the future.

## Update Behavior

The upgrade mechanism resembles the way used in Telink OTA App.
Read the [handbook on Telink Wiki site](https://wiki.telink-semi.cn/wiki/index.html) for details.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x248A`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### TelinkHidToolVer

The BCD tool version.

Since: 2.0.2

## External Interface Access

This plugin requires access to bluetooth GATT characteristics.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Liang-Jiazhi: @liangjiazhi-telink
