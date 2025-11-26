---
title: Plugin: Egis Fingerprint Sensor
---

## Introduction

The plugin used for update firmware for fingerprint sensors from Egis.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.egistec.usb`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1C7A&PID_05AE`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1C7A`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.14`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Jason Huang: @jasonhouang
