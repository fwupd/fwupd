---
title: Plugin: Intel Computer Vision Sensing
---

## Introduction

The Computer Vision Sensing camera is built by various silicon vendors and supported by Intel.

The Visual Sensing Controller a companion chip that helps make PCs more smart using AI.
For example, it could be used to detect the presence of a user and automatically adjust the
brightness of the screen.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in a packed binary file
format.

This plugin supports the following protocol ID:

* `com.intel.cvs`

## GUID Generation

These devices match from the standard I²C instance ID values, e.g.

* `IC2\NAME_INTC10DE:00`

The devices then create firmware matchable instance ID values which include the product PID, VID
and an additional optional OEM PID, e.g.

* `IC2\NAME_INTC10DE:00&VID_06CB&PID_06CB`
* `IC2\NAME_INTC10DE:00&VID_06CB&PID_06CB&OPID_12345678`

## Update Behavior

The device is probed by reading `cvs_ctrl_data_pre`. The firmware update is triggered by writing to
`cvs_ctrl_data_pre` and status is reported by polling `cvs_ctrl_data_fwupd`.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x06CB`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### IntelCvsMaxDownloadTime

The device maximum download time in ms, which defaults to 200s.

Since: 2.0.7

### IntelCvsMaxRetryCount

The device maximum retry count, which defaults to 5.

Since: 2.0.7

## External Interface Access

This plugin requires read/write access to `/dev/i2c*`.

## Version Considerations

This plugin has been available since fwupd version `2.0.7`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Richard Hughes: @hughsie
