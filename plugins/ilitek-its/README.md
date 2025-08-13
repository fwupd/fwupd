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

* `HIDRAW\VEN_222A&DEV_1234`
* `HIDRAW\VEN_222A&DEV_FF29`

Additional instance ID are added which corresponds to the FWID and/or sensor ID:

* `HIDRAW\VEN_222A&DEV_1234&FWID_1234`
* `HIDRAW\VEN_222A&DEV_1234&SENSORID_12`

Additional instance IDs are added which corresponds to the EDID PNP ID and product code:

* `HIDRAW\VEN_222A&DEV_1234&PNPID_ABC`
* `HIDRAW\VEN_222A&DEV_1234&PNPID_ABC&PCODE_1234`
* `HIDRAW\VEN_222A&DEV_1234&SENSORID_12&PNPID_ABC&PCODE_1234`

## Update Behavior

The device is updated after switching into bootloader mode, where touchscreen is nonfunctional.
After a successful firmware update, device will switch back to normal runtime mode.

## Vendor ID Security

The vendor ID is set from the HID vendor, e.g. `HIDRAW:0x222A`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### IlitekItsSensorIdMask

The sensor ID mask to use when constructing the instance ID.

Since: 2.0.14

## External Interface Access

This plugin requires ioctl `HIDIOCSFEATURE` and read access to `/dev/hidraw`.

## Version Considerations

This plugin has been available since fwupd version `2.0.14`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Joe Hong: @ILITEK-JoeHung
