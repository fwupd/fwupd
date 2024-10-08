---
title: Plugin: Intel CVS
---

## Introduction

The CVS camera is FIXME.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.intel.cvs`

## GUID Generation

These devices match from the standard i2c instance ID values, e.g.

* `IC2\NAME_INTC10DE:00`

The devices then create firmware matchable instance ID values which include the product
PID and VID, e.g.

* `IC2\NAME_INTC10DE:00&VID_06CB&PID_0701`

## Update Behavior

The device is probed by reading `cvs_ctrl_data_pre`. The firmware update is triggered by writing to
`cvs_ctrl_data_pre` and status is reported by polling `cvs_ctrl_data_fwupd`.

## Vendor ID Security

The vendor ID is set from the camera vendor, in this instance set to `USB:0x1234`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### IntelCvsMaxDownloadTime

The device maximum download time in ms, which defaults to 200s.

Since: 2.0.1

### IntelCvsMaxFlashTime

The device maximum flash time in ms, which defaults to 200s.

Since: 2.0.1

### IntelCvsMaxRetryCount

The device maximum retry count, which defaults to 5.

Since: 2.0.1

## External Interface Access

This plugin requires read/write access to `/dev/i2c*`.

## Version Considerations

This plugin has been available since fwupd version `2.0.1`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Intel: @github-username
