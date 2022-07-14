# Analogix

## Introduction

This plugin can flash the firmware of some Analogix billboard devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a Intel Hex file format. The resulting binary image is either:

* `OCM` section only
* `CUSTOM` section only
* Multiple sections excluded `CUSTOM` -- at fixed offsets and sizes

This plugin supports the following protocol ID:

* com.analogix.bb

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1F29&PID_7518`
* `USB\VID_050D&PID_008B`
* `USB\VID_047D&PID_80C8`
* `USB\VID_0502&PID_04C4`
* `USB\VID_14B0&PID_01D0`
* `USB\VID_14B0&PID_01D1`

## Update Behavior

The device is updated at runtime using USB control transfers.

## Vendor ID Security

The vendor ID is set from the USB vendor. The list of USB VIDs used is:

* `USB:0x1F29`
* `USB:0x050D`
* `USB:0x047D`
* `USB:0x0502`
* `USB:0x14B0`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
