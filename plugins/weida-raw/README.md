---
title: Plugin: Weida Raw
---

## Introduction

The plugin used for update firmware for touchscreen devices from Weida.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.weida.raw`

## GUID Generation

These devices use the standard TODO DeviceInstanceId values, e.g.

* `HIDRAW\VEN_2575&DEV_777F`.

## Update Behavior

The firmware is written to the device by switching to a flash program mode where the touchscreen
is nonfunctional, and the device will reset when the new firmware has been programmed.

## Vendor ID Security

The vendor ID is set from the udev vendor, in this instance set to 'HIDRAW:0x2575'

## External Interface Access

This plugin requires ioctl access to HIDIOCSFEATURE and HIDIOCGFEATURE.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Randy51: @randytry
* Weida: @chenhn123
