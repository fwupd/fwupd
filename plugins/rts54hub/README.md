---
title: Plugin: RTS54HUB
---

## Introduction

This plugin allows the user to update any supported hub and attached downstream
ICs using a custom HUB-based flashing protocol. It does not support any RTS54xx
device using the HID update protocol.

Other devices connected to the RTS54xx using I2C will be supported soon.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol IDs:

* `com.realtek.rts54`
* `com.realtek.rts54.i2c`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_0BDA&PID_5423`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0BDA`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.2.0`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### Rts54BlockSize

Defines the amount of data transferred in a single USB transaction, defaulting to 4096 bytes.

Since: 2.0.11

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Ricky Wu: @AnyProblem
