# SCSI

## Introduction

This plugin adds support for SCSI storage hardware. Devices are only enumerated
and are not updatable.

## GUID Generation

These device use the SCSI DeviceInstanceId values, e.g.

* `SCSI\VEN_HP&DEV_EG0900JETKB&REV_HPD4`
* `SCSI\VEN_HP&DEV_EG0900JETKB`
* `SCSI\VEN_HP`

## Vendor ID Security

The vendor ID is set from the vendor, for example set to `SCSI:HP`

## External Interface Access

This plugin requires only reading from sysfs.
