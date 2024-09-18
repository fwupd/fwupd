---
title: Plugin: FPC Fingerprint Sensor
---

## Introduction

The plugin used for update firmware for fingerprint sensors from FPC.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.fingerprints.dfupc`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_10A5&PID_FFE0`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=moh-device`

Device is a MOH device

### `Flags=rts`

Device is a RTS device.

### `Flags=legacy-dfu`

Device supports legacy DFU mode.

### `Flags=lenfy`

Device is a LENFY MOH device.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x10A5`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.8.6`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Jim Zhang: @jimzhang2
