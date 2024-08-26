---
title: Plugin: Synaptics CAPE
---

## Introduction

This plugin is used to update Synaptics CAPE based audio devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob.

This plugin supports the following protocol ID:

* `com.synaptics.cape`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1395&PID_0293`

These devices also use custom GUID values, e.g.

* `SYNAPTICS_CAPE\CX31993` (only-quirk)

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1395`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=use-in-report-interrupt`

Some devices will support IN_REPORT that allow host communicate with device over interrupt instead
of control endpoint.

Since: 1.7.0

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.7.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Simon Ho: @CNXT-Simon
