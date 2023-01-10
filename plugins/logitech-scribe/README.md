---
title: Plugin: Logitech Scribe
---

## Introduction

This plugin can upgrade the firmware on Logitech Video Collaboration products
(Scribe), using USB bulk transfer.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.logitech.vc.scribe`

## GUID Generation

These devices use the standard UDEV DeviceInstanceId values, e.g.

* `VIDEO4LINUX\VEN_046D&DEV_08E2'

## Quirk Use

This plugin uses the following plugin-specific quirks:

## Update Behavior

The peripheral firmware is deployed when the device is in normal runtime mode,
and the device will reset when the new firmware has been written.

## Design Notes

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x046D`

## External Interface Access

This plugin requires read access to `/dev/bus/usb`, '/dev/video0'.
This plugin requires the `UVCIOC_CTRL_QUERY` ioctl interface.
