---
title: Plugin: Nordic HID
---

## Introduction

This plugin is able to update the firmware for the hardware supported by [nRF Desktop](https://www.nordicsemi.com/Products/Reference-designs/nRF-Desktop) application reference design developed as part of the [nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/applications/nrf_desktop/README.html).
Tested with the following devices:

* [nRF52840 Development Kit](https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/boards/nordic/nrf52840dk/doc/index.html)
* [nRF52840 Dongle](https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/boards/nordic/nrf52840dongle/doc/index.html)
* Boards specific to the nRF Desktop reference design:
  * nRF52840 Gaming Mouse
  * nRF52832 Desktop Keyboard

The plugin is using Nordic Semiconductor [HID configuration channel](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/applications/nrf_desktop/doc/config_channel.html) to perform devices update.

## Firmware Format

The cabinet file contains ZIP archive prepared by Nordic Semiconductor.
This ZIP archive includes either 1 or 2 signed image blobs for the target device (one firmware blob per application update slot) and the `manifest.json` file with the metadata description.
At the moment only [nRF Secure Immutable Bootloader](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/samples/bootloader/README.html) ("B0") is supported and tested.

This plugin supports the following protocol ID:

* Nordic HID Config Channel: `com.nordic.hidcfgchannel`

## GUID Generation

For GUID generation the target board name, bootloader name and generation are used in addition to standard HIDRAW DeviceInstanceId values.
The generation string is an application-specific property that allows to distinguish configurations
that use the same board and bootloader, but are not interoperable.

GUID examples:

* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_B0&GEN_default`
* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_MCUBOOT&GEN_office`

Because handling of the generation parameter was introduced later, it is not supported by older versions of fwupd.
To ensure compatibility with firmware updates that were released before introducing the support for the generation parameter, devices with the `default` generation report an additional GUID that omits the generation parameter.

GUID examples (devices with generation set to `default` or without support for the generation parameter):

* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_B0`
* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_MCUBOOT`

## Quirk Use

This plugin also uses the following plugin-specific quirks:

### NordicHidBootloader

Explicitly set the expected bootloader type.
Only the `B0` bootloader can be set by the quirk file.
Other values can only be provided by device.
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

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be consulted before making major or functional changes:

* Marek Pieta: @MarekPieta
