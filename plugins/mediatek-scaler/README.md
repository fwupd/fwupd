---
title: Plugin: Mediatek Display Controller
---

## Introduction

This plugin updates the firmware of DisplayPort device made by Mediatek.

These devices communicate over I²C, via the DisplayPort aux channel. Devices
are declared by kernel graphic driver, and queried with custom DDC/CI command
to ensure the target devie.

This plugin polls every drm dp aux device to find out the `i2c-dev` that is
being used for DDC/CI communication. Devices should respond to a vendor specific
command otherwise the display controller is ignored as unsupported.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is written to the partition of the
device flash that is not currently running.

This plugin supports the following protocol ID:

* `com.mediatek.scaler`

## GUID Generation

Devices use instance ID that is composed of `Subsystem ID`, `Subsystem Model`, and
the `Hardware Version`. The hardware version is read from the device.

* `DISPLAY\VID_1028&PID_0C88&HWVER_2.1.2.1`
* `DISPLAY\VID_1028&PID_0C8A&HWVER_2.1.2.1`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written. On some hardware the
MST device may not enumerate if there is no monitor actually plugged in.

## Quirk Use

This plugin does not use quirks.

## Vendor ID security

The vendor ID is set from the PCI vendor, for instance `PCI:0x1028` on Dell systems.

## External Interface Access

This plugin requires access to i2c buses associated with the specified
DisplayPort aux channel, for instance `/dev/i2c-5` and `/dev/drm_dp_aux3`. Note that
the device number changes in various situations.

## Version Considerations

This plugin has been available since fwupd version `1.9.6`.
