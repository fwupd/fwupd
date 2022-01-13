# Nordic Semiconductor HID

## Introduction

This plugin is able flash the firmware on:

* nRF52-Desktop: nrf52840dk development kit

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

* Nordic HID Config Channel: com.nordic.hidcfgchannel

## GUID Generation

For GUID generation the standard HIDRAW DeviceInstanceId values are used
with the addition of the target board and bootloader name:

* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_B0` -> 22952036-c346-5755-9646-7bf766b28922
* `HIDRAW\VEN_1915&DEV_52DE&BOARD_nrf52840dk&BL_MCUBOOT` -> 43b38427-fdf5-5400-a23c-f3eb7ea00e7c

## Quirk Use

This plugin uses the following plugin-specific quirks:

### NordicHidBootloader

Explicitly set the expected bootloader type: "B0" or "MCUBOOT"
This quirk must be set for devices without support of `bootloader variant` DFU option.

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the HID vendor ID, in this instance set
to `HIDRAW:0x1915`.

## External Interface Access

This plugin requires ioctl `HIDIOCSFEATURE` and `HIDIOCGFEATURE` access.
