---
title: Plugin: Chrome OS EC
---

## Introduction

This plugin provides support for the firmware updates for Chrome OS EC
project based devices.

Initially, it supports the USB endpoint updater, but lays the groundwork for
future updaters which use other update methods other than the USB endpoint.

This is based on the chromeos ec project's [usb_updater2 application](https://chromium.googlesource.com/chromiumos/platform/ec/+/master/extra/usb_updater/usb_updater2.c).

Information about the USB update protocol is [available here](https://chromium.googlesource.com/chromiumos/platform/ec/+/master/docs/usb_updater.md).

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
the Google [fmap file format](https://www.chromium.org/chromium-os/firmware-porting-guide/fmap).

This plugin supports the following protocol ID:

* `com.google.usb.crosec`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_18D1&PID_501A`

## Update Behavior

The device usually presents in runtime mode, but on detach re-enumerates with
the same USB PID in an unlocked mode. On attach the device again re-enumerates
back to the runtime locked mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

## Vendor ID Security

The vendor ID is set from the USB vendor, which is set to various different
values depending on the model and device mode. The list of USB VIDs used is:

* `USB:0x18D1`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
