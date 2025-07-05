---
title: Plugin: SCSI
---

## Introduction

This plugin adds support for SCSI storage hardware. Most SCSI devices are enumerated and some UFS
devices may also be updatable.

Firmware is sent in chunks of 4kB by default and activated on next reboot only.
There is a quirk to change the chunk size for specific device.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `org.jedec.ufs`

## GUID Generation

These device use the SCSI DeviceInstanceId values, e.g.

* `SCSI\VEN_HP&DEV_EG0900JETKB&REV_HPD4`
* `SCSI\VEN_HP&DEV_EG0900JETKB`

## Vendor ID Security

The vendor ID is set from the vendor, for example set to `SCSI:HP`

## External Interface Access

This plugin requires only reading from sysfs for enumeration, but requires using a `sg_io ioctl`
for UFS updates.

## Version Considerations

This plugin has been available since fwupd version `1.7.6`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### ScsiWriteBufferSize

The block size used for WRITE_BUFFER commands to update the firmware.
Must be a multiple of 4k.
