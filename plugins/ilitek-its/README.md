---
title: Plugin: Ilitek Its
---

## Introduction

This plugin can update ILITEK touch controller's firmware.
Devices are enumerated via HID-over-I2C, USB and intel-quicki2c driver.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `tw.com.ilitek.its`

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_222A&DEV_0001`
* `HIDRAW\VEN_222A&DEV_FF25`

Additionally another instance ID is added which corresponds to the FWID or Sensor ID:

* `HIDRAW\VEN_222A&FWID_1234`
* `HIDRAW\VEN_222A&SENSORID_12`

Additionally another instance ID is added which corresponds to the EDID:

* `DRM\VEN_ABC`
* `DRM\VEN_ABC&DEV_1234`
* `DRM\VEN_ABC&DEV_1234&SENSORID_12`

## Update Behavior

The device is updated after switching into bootloader mode, where touchscreen is nonfunctional.
After a successful firmware update, device will switch back to normal runtime mode.

## Vendor ID Security

The vendor ID is set from the HID vendor, e.g. `HIDRAW:0x222A`

## External Interface Access

This plugin requires ioctl `HIDIOCSFEATURE` and read access to `/dev/hidraw`.

## Version Considerations

This plugin has been available since fwupd version `2.0.14`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Joe Hong: @ILITEK-JoeHung
