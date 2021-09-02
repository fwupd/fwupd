# SteelSeries

## Introduction

This plugin allows to update firmware on SteelSeries gamepads:

* Stratus Duo
* Stratus Duo USB wireless adapter
* Proton

SteelSeries gaming mice support is limited by getting the correct version
number. These mice have updatable firmware but so far no updates are available
from the vendor.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1038&PID_1702&REV_0001`
* `USB\VID_1038&PID_1702`
* `USB\VID_1038`

## Update Behavior

### Gamepad

Gamepad and/or its wireless adapter must be connected to host via USB cable
to apply an update. The device is switched to bootloader mode to flash
updates, and is reset automatically to new firmware after flashing.

### Mice

The device is not upgradable and thus requires no vendor ID set.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1038`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
