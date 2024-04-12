---
title: Plugin: audio-s5gen2
---

## Introduction

Firmware Update Plug-in for Qualcomm Voice & Music Series 5 Gen 1 and Gen 2, and Series 3 Gen 1, Gen 2 and Gen 3.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

The DFU file format is covered in documentation from Qualcomm, referenced by 80-CH281-1.

This plugin supports the following protocol ID:

* `com.qualcomm.s5gen2`

## GUID Generation

These devices use the standard  DeviceInstanceId values, e.g.

* `USB\VID_0A12&PID_4007`

## Update Behavior

The device is updated in runtime mode and rebooted with a new version.
The commit command should be used after the update process is done, otherwise
the device will reboot with the previous firmware version.

The upgrade protocol and update behivior are specified in documentation from Qualcomm,
referenced by 80-CH281-1 and 80-CU043-1.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0A12`

## Quirk Use

This plugin uses the following plugin-specific quirks:

* no specific quirks

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.9.16`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Denis Pynkin: @d4s
