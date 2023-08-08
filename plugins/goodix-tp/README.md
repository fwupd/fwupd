---
title: Plugin: Goodix Touch Controller Sensor
---

## Introduction

This plugin allows updating Touchpad devices from Goodix. Devices are enumerated
using HID and raw I²C nodes. The I²C mode is used for ABS devices and firmware
recovery of HID devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `com.goodix.goodixtp`

## GUID Generation

These device uses the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_27C6`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x01E0`

## External Interface Access

This plugin requires ioctl access to `HIDIOCSFEATURE` and `HIDIOCGFEATURE`.

## Version Considerations

This plugin has been available since fwupd version `1.9.2`.
