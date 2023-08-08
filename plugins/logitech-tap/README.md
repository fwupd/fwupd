---
title: Plugin: Logitech Tap — Video Collaboration
---

## Introduction

This plugin can upgrade the firmware on Logitech Video Collaboration products
(Tap), using ioctl.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.logitech.hardware.tap`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `HIDRAW\VEN_046D&DEV_0872`
* `VIDEO4LINUX\VEN_046D&DEV_0876`

## Quirk Use

This plugin uses the following plugin-specific quirks:

## Update Behavior

The peripheral firmware is deployed when the device is in normal runtime mode,
and the device will reset when the new firmware has been written.

## Design Notes

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x046D`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
This plugin requires the ioctl interfaces: `UVCIOC_CTRL_QUERY`, `HIDIOCGFEATURE`, `HIDIOCSFEATURE`, `HIDIOCGINPUT`.

## Version Considerations

This plugin has been available since fwupd version `1.9.2`.
