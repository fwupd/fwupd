# Wistron Dock

## Introduction

Wistron use a generic flashing protocol for dock devices supplied to various OEMs. The protocol is
compatible with designs that use Nuvoton, Infineon, and GD MCUs.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in a zipped file format.
The archive must contain exactly one file with each of these extensions:

* `.wdfl.sig`
* `.wdfl`
* `.bin`

This plugin supports the following protocol ID:

* com.wistron.dock

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_0FB8&PID_0010&REV_0001`
* `USB\VID_0FB8&PID_0010`
* `USB\VID_0FB8`

Devices also have additional instance IDs which corresponds MCU type, e.g.

* `USB\VID_0FB8&PID_0010&MCUID_M4521`

## Update Behavior

The device enters DFU mode, then writes the fixed-size WDFL signature and WDFL data, then writes
blocks of variable sized data. Finally it clears DFU mode and the user can re-plug the USB-C cable
to trigger the update.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this example set to `USB:0x0FB8`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
