---
title: Plugin: UF2
---

## Introduction

This plugin allows the user to update any supported UF2 Device by writing
firmware onto a mass storage device.

A UF2 device exposes a VFAT block device which has a virtual file
`INFO_UF2.TXT` where metadata can be read from. It may also have a the
current firmware exported as a file `CURRENT.UF2` which is in a 512
byte-block UF2 format.

Writing any file to the MSD will cause the firmware to be written.
Sometimes the device will restart and the volume will be unmounted and then
mounted again. In some cases the volume may not “come back” until the user
manually puts the device back in programming mode.

Match the block devices using the VID, PID and UUID, and then create a
UF2 device which can be used to flash firmware.

Note: We only read metadata from allow-listed IDs to avoid causing regressions
on non-UF2 volumes. To get the UUID you can use commands like:

    udisksctl info -b /dev/sda1

The UF2 format is specified [here](https://github.com/Microsoft/uf2>).

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
the UF2 file format.

This plugin supports the following protocol ID:

* `com.microsoft.uf2`

## GUID Generation

These devices use standard USB DeviceInstanceId values, e.g.

* `USB\VID_1234&PID_5678`
* `USB\VID_1234&PID_5678&UUID_E478-FA50`

Additionally, the UF2 Board-ID and Family-ID may be added:

* `UF2\BOARD_{Board-ID}`
* `UF2\FAMILY_{Family-ID}`

## Update Behavior

The firmware is deployed when the device is inserted, and the firmware will
typically be written as the file is copied.

## Vendor ID Security

The vendor ID is set from the USB vendor.

## External Interface Access

This plugin requires permission to mount, write a file and unmount the mass
storage device.

## Version Considerations

This plugin has been available since fwupd version `1.7.4`.
