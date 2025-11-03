---
title: Plugin: Legion HID
---

## Introduction

The Legion HID plugin is used for interacting with the MCU on Legion Go 2 devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.lenovo.legion-hid`

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_17EF&DEV_61EB`

Additionally, for child devices two custom instance IDs are created:

* `HIDRAW\VEN_17EF&DEV_61EB&CHILD_LEFT`
* `HIDRAW\VEN_17EF&DEV_61EB&CHILD_RIGHT`

## Update Behavior

The device will restart after update.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x17EF`

## Quirk Use

This plugin uses the following plugin-specific quirks:

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.18`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

@gzmarshall
