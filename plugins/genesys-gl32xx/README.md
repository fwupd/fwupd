---
title: Plugin: Genesys GL322x/GL323x
---

## Introduction

The GL3224/GL3232 families are USB3 card reader products.

## Firmware Format

This plugin supports the following protocol ID:

* `com.genesys.gl32xx`

## GUID Generation

These devices use the standard UDEV DeviceInstanceId values, e.g.

* `[BLOCK\VEN_05E3&DEV_XXXX]`

## Update Behavior

The device is switched to ROM mode for the update and the device must be reset
the firmware update/dump to return back to normal mode.

For 323x family the expected firmware size is `0x01C000`, and `0x010000` for 3224.

## Vendor ID Security

The vendor ID is set from the udev vendor, in this instance set to `BLOCK:0x05E3`

## External Interface Access

This plugin requires read/write access to `/dev/sd*` block devices and
requires using a `sg_io ioctl` for interaction with the device.

## Version Considerations

This plugin has been available since fwupd version `1.9.3`.
