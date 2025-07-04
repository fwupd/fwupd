---
title: Plugin: ELAN Keyboard
---

## Introduction

The ELAN keyboard interface is used by many OEMs, both in i2c mode and in USB mode -- although
only updating over USB is supported by this plugin at this time.
The microcontroller used is the EM85F684A which is loaded with an ELAN-specific bootloader.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in a packed binary file
format. This is split, with 0x2000 bytes for the bootloader, and 0x6000 for the application runtime
code.

This plugin supports the following protocol ID:

* `com.elan.kbd`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1018&PID_1006`

## Update Behavior

The device flash is updated from a 0x2000 offset.
The plugin will disconnect the device into a bootloader mode to perform the update.

## Vendor ID Security

The vendor ID is set from the runtime USB vendor, in this instance set to `USB:0x1018`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.5`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Richard Hughes: @hughsie
