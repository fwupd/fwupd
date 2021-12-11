# UF2 Devices

## Introduction

This plugin allows the user to update any supported UF2 Device by writing
firmware onto a mass storage device.

This is all specified here: <https://github.com/Microsoft/uf2>

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
the UF2 file format.

This plugin supports the following protocol ID:

* com.microsoft.uf2

## GUID Generation

These devices use the standard block DeviceInstanceId values, e.g.

* `BLOCK\UUID_E478-FA50`
* `BLOCK\LABEL_FWUPDATE`

Additionally, the UF2 Board-ID and Family-ID may be added:

* `UF2\BID_{Board-ID}`
* `UF2\FID_{Family-ID}`

## Update Behavior

The firmware is deployed when the device is inserted, and the firmware will
typically be written as the file is copied.

## Vendor ID Security

The vendor ID is set from the USB vendor.

## External Interface Access

This plugin requires permission to mount, write a file and unmount the mass
storage device.
