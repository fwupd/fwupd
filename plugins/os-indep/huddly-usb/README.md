---
title: Plugin: Huddly Usb
---

## Introduction

This plugin supports performing firmware upgrades on Huddly L1 and S1 video conferencing cameras
connected via the Huddly USB adapter.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.huddly.usb`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_2BD9&PID_A032`

## Update Behavior

* The firmware is deployed when the device in in normal runtime mode.
* The device will reboot and re-enumerate after the firmware has been written.
* The firmware file is deployed again after the reboot for verification.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x2BD9`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Huddly: @LarsStensen
