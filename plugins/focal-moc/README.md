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
the SOH header, then streams the image in 1 KiB STX blocks followed by a zero-padded EOT block for
any remainder.  The update stream is limited to 255 1 KiB data frames because the protocol uses an
8-bit frame number.

This plugin supports the following protocol ID:

* `com.focal.moc`

## GUID Generation

These devices use the standard USB instance ID values such as `USB\VID_2808&PID_XXXX`.

## Update Behavior

The firmware is deployed when the device is in normal runtime mode.
The plugin first switches the device to bootloader (IAP) mode (`detach`), then streams the firmware
image using the 3-phase SOH/STX/EOT protocol.
The device reboots automatically after the final ACK; the plugin then issues a return-to-app
command (`attach`) in case the device has not yet rebooted.
The device re-enumerates after each mode switch.

Before the transfer the wake-up command confirms the device is responsive rather than discovering
a dead device part way through the frames.  A bootloader is probed once to tell its legacy status
codes apart from the unified ones, so an ambiguous failure is not reported as though it had a
known meaning.  Reply parsing locates the checksum from the declared length, allowing only the
padding the firmware appends past the declared frame.

Both modes use the same USB product ID, so the mode is taken from the `_APP_` or `_IAP_` marker in
the version string reported by the device.
A device left in bootloader mode by an interrupted update is detected as such, and an update can
start without another mode switch.

## Vendor ID Security

The vendor ID is set from the USB vendor ID, in this instance `USB:0x2808`.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.1.8`.
