---
title: Plugin: Novatek Touchscreen
---

## Introduction

This plugin allows updating Novatek touchscreen controllers. Devices are enumerated using HIDRAW.

## Firmware Format

The daemon will decompress the cabinet archive and extract the firmware blob.

This plugin expects a raw binary payload with an end flag of `NVT` and a maximum size of 320KB.

This plugin supports the following protocol ID:

* `tw.com.novatek.ts`

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_0603&DEV_F203`

An additional instance ID is added which corresponds to the PID read from flash:

* `HIDRAW\VEN_0603&PJID_60AA`

## Update Behavior

The device is updated after switching into idle mode, where the touchscreen is nonfunctional.
The firmware is written using GCM commands with erase, program, and verify, and the device is reset
back to normal runtime mode on success.

## Vendor ID Security

The vendor ID is set from the HID vendor, e.g. `HIDRAW:0x0603`

## External Interface Access

This plugin requires ioctl access to `HIDIOCSFEATURE` and `HIDIOCGFEATURE` and
read/write access to `/dev/hidraw`.
