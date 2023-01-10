---
title: Plugin: USI Dock
---

## Introduction

This plugin uses the MCU to write all the dock firmware components. The MCU version
is provided by the DMC bcdDevice.

This plugin supports the following protocol ID:

* `com.usi.dock`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_17EF&PID_7226&REV_0001`
* `USB\VID_17EF&PID_7226`
* `USB\VID_17EF`

Additionally, some extra "component ID" instance IDs are added.

* `USB\VID_17EF&PID_7226&CID_TBT4`
* `USB\VID_17EF&PID_7226&CID_USB3`

## Update Behavior

The device usually presents in runtime mode, but on detach re-enumerates with
the same USB PID in an unlocked mode. On attach the device again re-enumerates
back to the runtime locked mode.

## Vendor ID Security

The vendor ID is set from the USB vendor.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
