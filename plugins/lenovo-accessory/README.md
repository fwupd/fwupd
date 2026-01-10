---
title: Plugin: Lenovo Accessory
---

## Introduction

The Lenovo Accessory plugin is designed to support the standardized firmware update protocol for Lenovo peripherals.
This is an in-house protocol developed by Lenovo that is MCU-agnostic and supports multiple transports.

## Firmware Format

The daemon decompresses the cabinet archive and extracts a firmware blob in a packed binary format.
The payload is currently unsigned.

## Protocol Support

This plugin supports the Lenovo standardized HID/BLE update protocol.
Current implementation focuses on the HID transport, with plans to support BLE custom services in the future.

* **USB HID**: Uses Get/Set Feature reports.
* **Bluetooth LE**: Uses a custom UUID service.

## GUID Generation

These devices use standard HID instance IDs for identification.
When in bootloader mode, devices may share a common PID (e.g., `0x6194`), so specific instance ID matching is used to differentiate devices.

Typical IDs include:

* `HIDRAW\VID_17EF&PID_629D`
* `HIDRAW\VID_17EF&PID_6201`

## Update Behavior

The device reboots into a dedicated bootloader mode for the update process.
A final restart is performed after the firmware is successfully written to the device.

## Vendor ID Security

The vendor ID is derived from the USB vendor, which is `USB:0x17EF`.

## External Interface Access

This plugin communicates with devices via the Linux **hidraw** subsystem (`/dev/hidraw*`).
Access is granted through the installed udev rules.
