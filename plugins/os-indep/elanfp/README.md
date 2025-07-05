---
title: Plugin: Elan Fingerprint Sensor
---

## Introduction

The plugin used for update firmware for fingerprint sensors from Elan.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `tw.com.emc.elanfp`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=usb-bulk-transfer`

Use USB bulk transfer.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_04F3&PID_0C7E`

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x04F3`

## Version Considerations

This plugin has been available since fwupd version `1.7.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Michael Cheng: @MichaelCheng04
