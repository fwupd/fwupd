---
title: Plugin: Nordic HID
---

## Introduction

This plugin is able flash the firmware for the hardware supported by `nRF52-Desktop`.
Tested with the following devices:

* [nrf52840dk development kit](https://www.nordicsemi.com/Products/nRF52840)
* [nRF52840 Dongle](https://www.nordicsemi.com/Products/Development-hardware/nrf52840-dongle)
* nRF52840 Gaming Mouse
* nRF52832 Desktop Keyboard

The plugin is using Nordic Semiconductor
[HID config channel](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/applications/nrf_desktop/doc/config_channel.html)
to perform devices update.

## Firmware Format

The cabinet file contains ZIP archive prepared by Nordic Semiconductor.
This ZIP archive includes 2 signed image blobs for the target
device, one firmware blob per application slot, and the `manifest.json` file with the metadata description.
At the moment only [nRF Secure Immutable Bootloader](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/samples/bootloader/README.html#bootloader)
aka "B0" is supported and tested.

This plugin supports the following protocol ID:

* Nordic HID Config Channel: `com.nordic.hidcfgchannel`

## GUID Generation

For GUID generation the target board name, bootloader name and generation are used in addition to standard HIDRAW DeviceInstanceId values.
The generation string is an application-specific property that allows to distinguish configurations
that use the same board and bootloader, but are not interoperable.

GUID examples:

* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_B0&GEN_default` -> b76d19e5-d745-5c0b-b870-b1b6d78e3c63
* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_MCUBOOT&GEN_office` -> 1728d5e4-e535-57f9-addc-a6c3765f81db

Because handling of the generation parameter was introduced later, it is not supported by older versions of fwupd.
To ensure compatibility with firmware updates that were released before introducing the support for the generation parameter, devices with the `default` generation report an additional GUID that omits the generation parameter.

GUID examples (devices with generation set to `default` or without support for the generation parameter):

* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_B0` -> 22952036-c346-5755-9646-7bf766b28922
* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_MCUBOOT` -> 43b38427-fdf5-5400-a23c-f3eb7ea00e7c

## Quirk Use

This plugin uses the following plugin-specific quirks:

### NordicHidBootloader

Explicitly set the expected bootloader type: "B0", "MCUBOOT" or "MCUBOOT+XIP".
This quirk must be set for devices without support of `bootloader variant` DFU option.

### NordicHidGeneration

Explicitly set the expected generation.
Only the `default` generation can be set by the quirk file.
Other values can only be provided by device.
This quirk must be set for devices that do not support the `devinfo` DFU option.

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the HID vendor ID, in this instance set
to `HIDRAW:0x1915`.

## External Interface Access

This plugin requires ioctl `HIDIOCSFEATURE` and `HIDIOCGFEATURE` access.

## Version Considerations

This plugin has been available since fwupd version `1.7.3`.
