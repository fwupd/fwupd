---
title: Plugin: Legion HID 2
---

## Introduction

The Legion HID plugin is used for interacting with the MCU on some Legion devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.lenovo.legion-hid2`

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `USB\VID_1A86&PID_E310`

## Update Behavior

The device will restart after update.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1A86`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

Since: 2.0.0

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

@superm1
