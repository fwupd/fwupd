---
title: Plugin: TI TPS6598x
---

## Introduction

The TPS65982DMC is a dock management controller for docks, hubs, and monitors implementing TI PD
controllers. Suitable Power Delivery (PD) Controller devices include TPS6598x which are updated as
part of the DMC firmware. There may be multiple PD devices attached to each DMC device.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob as a SHA-256+RSA-3072
signed binary file.

This plugin supports the following protocol ID:

* `com.ti.tps6598x`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_0451&PID_ACE1&REV_0001`
* `USB\VID_0451&PID_ACE1`
* `USB\VID_0451`

Child devices also have an additional instance IDs which corresponds to the index, e.g.

* `USB\VID_2188&PID_5988&REV_0714&PD_00`
* `USB\VID_2188&PID_5988&PD_00`

## Update Behavior

The device usually presents in runtime mode.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0451`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.8.9`.
