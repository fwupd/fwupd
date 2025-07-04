---
title: Plugin: Algoltek USB
---

## Introduction

This plugin supports the firmware upgrade of DisplayPort over USB-C to HDMI converter provided by Algoltek, Inc. These DisplayPort over USB-C to HDMI converters can be updated through multiple interfaces, but this plugin is only designed for the USB interface.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `tw.com.algoltek.usb`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_25A4&PID_9311`
* `USB\VID_25A4&PID_9312`
* `USB\VID_25A4&PID_9313`
* `USB\VID_25A4&PID_9411`
* `USB\VID_25A4&PID_9421`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the device will reset when the new firmware has been programmed.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=ers-skip-first-sector`

Skip erasing the first sector, needed for AG9411 and AG9421 products.

Since: 2.0.2

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x25A4`.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.9.11`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

Mason Lyu: @MasonLyu
