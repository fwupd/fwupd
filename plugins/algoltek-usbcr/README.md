---
title: Plugin: Algoltek Usbcr
---

## Introduction

This plugin supports firmware upgrade for USB card reader products provided by Algoltek, Inc.

## Firmware Format

This plugin supports the following protocol ID:

* `com.algoltek.usbcr`

## GUID Generation

These devices use the standard UDEV DeviceInstanceId values, e.g.

* `[BLOCK\VEN_058F&DEV_8461]`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the device will reset when
the new firmware has been programmed.

## Vendor ID Security

The vendor ID is set from the udev vendor, in this instance set to `BLOCK:0x058F`

## External Interface Access

This plugin requires read/write access to `/dev/sd*` block devices and
requires using a `sg_io ioctl` for interaction with the device.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.
