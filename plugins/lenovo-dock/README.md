---
title: Plugin: Lenovo Dock
---

## Introduction

The Lenovo dock plugin is used by various Lenovo docking stations.

## Firmware Format

The daemon will decompress the cabinet archive and extract two firmware binary files from a zip archive payload.
In this zip file there must be two files:

* `*_composite_image.bin`
* `*_usage_information_table.bin`

This plugin supports the following protocol ID:

* `com.lenovo.dock`

## GUID Generation

These devices use the standard HIDRAW DeviceInstanceId values, e.g.

* `HIDRAW\VEN_17EF&DEV_111E`

## Update Behavior

The device is unlocked and the firmware images are sent to the device.
The dock is then rebooted where the firmware is written, and then the dock re-appears.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### LenovoDockImagePid

The PID to expect from the image, falling back to the device PID if not set.

Since: 2.1.2

## Vendor ID Security

The vendor ID is set from the HID vendor, in this instance set to `USB:0x17EF`

## External Interface Access

This plugin requires read/write access to `/dev/hidraw*`.

## Version Considerations

This plugin has been available since fwupd version `2.1.2`.
