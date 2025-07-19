---
title: Plugin: Parade USB Hub
---

## Introduction

Parade USB hubs such as PS5512 have a built-in SPI-Master engine for programming external SPI ROM
flash. The USB2.0 hub device can do control transfers to control an internal mailbox.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in a packed binary file
format.

This plugin supports the following protocol ID:

* `com.paradetech.usbhub`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_273F&PID_1001`

## Update Behavior

* Load 384KB SPI ROM data buffer
* Specify target SPI ROM bank, ex: HUB_FW#1 or HUB_FW#2
* Set UFP Disconnect Flag register to notify firmware that we are doing firmware update
* Acquire SPI
* Unprotect SPI ROM bank
* Erase SPI ROM bank
* Update SPI ROM bank
* Protect SPI ROM bank
* Verify checksum for the just updated SPI ROM bank

It takes around 90 seconds to update the FW#1 64KB firmware.
FW#2 is the factory default with a known good firmware.
Please do a full power cycle to make new firmware take effect.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x273F`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### ParadeUsbhubChip

Set the chip used, e.g. `ps188` or `ps5512`, defaulting to the latter.

Since: 2.0.2

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Hub Lin: @hublin2024
* Jimmy Tu: @jimmytu5167
* Andy Chu: @andychu5168
