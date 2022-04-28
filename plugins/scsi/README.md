# SCSI

## Introduction

This plugin adds support for SCSI storage hardware. Most SCSI devices are enumerated and some UFS
devices may also be updatable.

Firmware is sent in 4kB chunks and activated on next reboot only.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* org.jedec.ufs

## GUID Generation

These device use the SCSI DeviceInstanceId values, e.g.

* `SCSI\VEN_HP&DEV_EG0900JETKB&REV_HPD4`
* `SCSI\VEN_HP&DEV_EG0900JETKB`
* `SCSI\VEN_HP`

## Vendor ID Security

The vendor ID is set from the vendor, for example set to `SCSI:HP`

## External Interface Access

This plugin requires only reading from sysfs for enumeration, but requires using a `sg_io ioctl`
for UFS updates.
