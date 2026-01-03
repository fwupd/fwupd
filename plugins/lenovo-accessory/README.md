---
title: Plugin: Lenovo Accssory
---

## Introduction

The Lenovo Accessory plugin is used for interacting with the MCU on some Lenovo devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.lenovo.accessory.input.hid`

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `HIDRAW\VID_17EF&PID_629D`
* `HIDRAW\VID_17EF&PID_6201`

## Update Behavior

The device will run in bootloader.
The device will restart after update.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x17EF`

## External Interface Access

This plugin communicates with devices via the Linux **hidraw** subsystem  
(`/dev/hidraw*`), not raw USB nodes.  
Access is granted by the installed udev rule:
