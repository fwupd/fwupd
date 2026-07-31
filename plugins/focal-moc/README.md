---
title: Plugin: FocalTech Fingerprint Sensor
---

## Introduction

This plugin updates firmware for FocalTech Match-On-Chip (MOC) fingerprint sensors over USB bulk
transfer.

The communication protocol is derived from the reference `UpgradeTool` source code provided by
FocalTech Systems Co., Ltd.

## Firmware Format

The daemon decompresses the cabinet archive and extracts a flat binary firmware blob.
No additional wrapping is expected.  The plugin computes a CRC32 over the full image, sends it in
the SHO header, then streams the image in 1 KiB STX blocks followed by a zero-padded EOT block for
any remainder.

This plugin supports the following protocol ID:

* `com.focal.moc`

## GUID Generation

These devices use the standard USB instance ID values such as `USB\VID_2808&PID_XXXX`.

## Update Behavior

The firmware is deployed when the device is in normal runtime mode.
The plugin first switches the device to bootloader (IAP) mode (`detach`), then streams the firmware
image using the 3-phase SHO/STX/EOT protocol.
The device reboots automatically after the final ACK; the plugin then issues a return-to-app
command (`attach`) in case the device has not yet rebooted.
The device re-enumerates after each mode switch.

## Vendor ID Security

The vendor ID is set from the USB vendor ID, in this instance `USB:0x2808`.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.1.8`.
