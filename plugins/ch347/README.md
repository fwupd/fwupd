---
title: Plugin: CH347
---

## Introduction

The CH347 is an affordable SPI programmer.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob of unspecified format.

This plugin supports the following protocol ID:

- `org.jedec.cfi`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

- `USB\VID_1A86&PID_55DB&REV_0304`
- `USB\VID_1A86&PID_55DB`

## Update Behavior

The device programs devices in raw mode, and can best be used with `fwupdtool`.

To write an image, use `sudo fwupdtool --plugins ch347 install-blob firmware.bin` and to backup
the contents of a SPI device use `sudo fwupdtool --plugins ch347 firmware-dump backup.bin`

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1A86`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.8.14`.
