---
title: Plugin: Raydium Touch Controller Sensor
---

## Introduction

This plugin allows updating Touch Screen devices from Raydium. Devices are 
enumerated using HID nodes for detection and firmware updates.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `com.raydium.raydiumtp`

## GUID Generation

These device uses the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_2386&DEV_8C01`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x2386`

## External Interface Access

This plugin requires ioctl access to `HIDIOCSFEATURE` and `HIDIOCGFEATURE`.

## Version Considerations

This plugin has been available since fwupd version `2.1.1`.
