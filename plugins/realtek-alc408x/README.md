---
title: Plugin: Realtek ALC408x
---

## Introduction

The Realtek ALC408x are USB 2.0 audio controllers with an 8051 microcontroller that runs
firmware from internal flash. This plugin updates that flash over USB using vendor
requests on the control endpoint, so the kernel audio driver can stay bound to the device while
the new firmware is staged. It currently accepts only the ALC4080.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in a packed binary
file format.

The image is exactly 64 KiB. A big-endian header at offset `0x1000` holds the magic value
`0x12345678`, the chip number (`0x4080`) and the version as two 32-bit values, which are
shown as five fixed-width hex fields: `0x10020003` and `0x00040005` read as
`1.002.0003-0004.0005`.

This plugin supports the following protocol ID:

* `com.realtek.alc408x`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_0955&PID_CF0A`

## Update Behavior

The internal flash holds the boot image in bank 0 at `0x00000` and a staging image in bank 1
at `0x10000`. The plugin erases bank 1, writes the new image to it and reads it back to verify it.
Only then does it clear the tag in the bank 0 header; until that point the device keeps booting
the old image, so an interrupted update leaves the device working.

The plugin then resets the device. The boot ROM sees the cleared tag, copies bank 1 over bank 0
and boots the new image, and the device is back on the bus a few seconds later. No host reboot is
required.

The device runs in its normal runtime mode for the whole update and has no separate bootloader
mode.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0955`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.2.1`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* NVIDIA: @vraghav8
