---
title: Plugin: Realtek Billboard
---
## Introduction

This plugin updates the firmware of Realtek USB Billboard Class devices.
The Billboard Class (USB class `0x11`) allows the device to expose its
characteristics to the operating system, while the vendor control transfers
are used to program the on-board flash.

## Firmware Format

The daemon will decompress the cabinet archive and extract a raw binary
firmware blob. There is no specific header or container format; the payload
is the flash contents of the device, written in 256 byte chunks using vendor
control transfers.

This plugin supports the following protocol ID:

* `com.realtek.billboard`

## GUID Generation

These devices use the standard USB Device InstanceId values, e.g.

* `USB\VID_32AC&PID_0029`

## Update Behavior

The device supports dual-bank updating: the spare bank is erased, the new
firmware is written, and a user flag is updated to mark the new firmware as
the active one. The device re-enumerates after the update and must be plugged
into a USB-C port that exposes the Billboard function.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x32AC`
only when the USB device vendor matches.

## External Interface Access

This plugin requires the following access:

* USB device — `USB\VID_32AC&PID_0029`
