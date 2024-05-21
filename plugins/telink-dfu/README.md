---
title: Plugin: Telink Dfu
---

## Introduction

This plugin is to upgrade Telink OTA-compliant devices.
Supported devices:

* [8208 Dual Keyboard](./)

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:


The cabinet file contains ZIP archive prepared by Nordic Semiconductor.
This ZIP archive includes 2 signed image blobs for the target
device, one firmware blob per application slot, and the `manifest.json` file with the metadata description.
At the moment only [nRF Secure Immutable Bootloader](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/samples/bootloader/README.html#bootloader)
aka "B0" is supported and tested.

The cabinet file prepared by Telink Semiconductor includes a validated firmware image `firmware.bin`, an accompanying `manifest.json` of firmware description. At the moment only the version field takes effect.

This plugin supports the following protocol ID:

* Telink DFU over BLE: `com.telink.dfu`

## GUID Generation

These devices use the standard BLUETOOTH DeviceInstanceId values, e.g.

* `BLUETOOTH\VID_248A&PID_8208`

HIDRAW is expected to be supported in the future.

## Update Behavior

The upgrade mechanism resembles the way used in Telink OTA App. Read the [handbook on Telink Wiki site](https://wiki.telink-semi.cn/wiki/index.html) for details.

## Vendor ID Security

The vendor ID is set from the HID vendor, in this instance set to `USB:0x248A`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### TelinkDfuBootType

This is reserved for bootloader variants and should be set to either `beta` or `otav1` for now.

Since fwupd version 1.9.9

### TelinkDfuBoardType

The bla bla bla.

Since fwupd version 1.9.9

## External Interface Access

This plugin requires read/write access to `BLUETOOTH`.
This plugin requires ioctl `HIDIOCSFEATURE` and `HIDIOCGFEATURE` access when DFU over HID is supported.

## Version Considerations

This plugin has been available since fwupd version `1.9.9`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Liang-Jiazhi: @liangjiazhi-telink
