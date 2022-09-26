# FPC Fingerprint Sensor

## Introduction

The plugin used for update firmware for fingerprint sensors from FPC.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* com.fingerprints.dfupc

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_10A5&PID_FFE0&REV_0001`
* `USB\VID_10A5&PID_FFE0`
* `USB\VID_10A5`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x10A5`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
