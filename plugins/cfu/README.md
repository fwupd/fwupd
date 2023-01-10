---
title: Plugin: CFU - Component Firmware Update
---

## Introduction

CFU is a protocol from Microsoft to make it easy to install firmware on HID devices.

This protocol is unique in that it requires has a pre-download phase before sending the firmware to
the microcontroller. This is so the device can check if the firmware is required and compatible.
CFU also requires devices to be able to transfer the entire new transfer mode in runtime mode.

See <https://docs.microsoft.com/en-us/windows-hardware/drivers/cfu/cfu-specification> for more
details.

This plugin supports the following protocol ID:

* `com.microsoft.cfu`

## GUID Generation

These devices use standard USB DeviceInstanceId values, as well as two extra for the component ID
and the bank, e.g.

* `HIDRAW\VEN_17EF&DEV_7226&CID_01&BANK_1`
* `HIDRAW\VEN_17EF&DEV_7226&CID_01`
* `HIDRAW\VEN_17EF&DEV_7226`

## Update Behavior

The device has to support runtime updates and does not have a detach-into-bootloader mode -- but
after the install has completed the device still has to reboot into the new firmware.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `HIDRAW:0x17EF`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
