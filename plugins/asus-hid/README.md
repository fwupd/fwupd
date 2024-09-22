---
title: Plugin: Asus HID
---

## Introduction

The ASUS HID plugin is used for interacting with the ITE MCUs on Asus
devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.asus.hid`

## GUID Generation

These devices use the a DeviceInstanceId value that also reflects the microcontroller ID.

* `USB\VID_0B05&PID_1ABE&PART_RC72LA`

## Update Behavior

The device will restart after update.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0B05`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### AsusHidNumMcu

The number of MCUs connected to the USB endpoint.

Since: 2.0.0

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

@superm1
