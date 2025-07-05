---
title: Plugin: ColorHug
---

## Introduction

The ColorHug is an affordable open source display colorimeter built by
Hughski Limited. The USB device allows you to calibrate your screen for
accurate color matching.

ColorHug versions 1 and 2 support a custom HID-based flashing protocol, but
version 3 (ColorHug+) has now switched to DFU.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.hughski.colorhug`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_273F&PID_1001`

## Update Behavior

The device usually presents in runtime mode, but on detach re-enumerates with a
different USB PID in a bootloader mode. On attach the device again re-enumerates
back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

### `Flags=halfsize`

Some devices have a compact memory layout and the application code starts earlier.

Since: 1.0.3

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x273F`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `0.8.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Richard Hughes: @hughsie
