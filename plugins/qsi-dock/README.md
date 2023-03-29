---
title: Plugin: QSI Dock
---

## Introduction

This plugin uses the MCU to write all the dock firmware components. The MCU version
is provided by the DMC bcdDevice.

This plugin supports the following protocol ID:

* `com.qsi.dock`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_047D&PID_808D&REV_0001`
* `USB\VID_047D&PID_808D`
* `USB\VID_047D`

## Update Behavior

The device usually presents in runtime mode, but on detach re-enumerates with
the same USB PID in an unlocked mode. On attach the device again re-enumerates
back to the runtime locked mode.

## Vendor ID Security

The vendor ID is set from the USB vendor.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.8.8`.
