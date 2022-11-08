# Texas Instruments TPS6598x

## Introduction

The TPS65982DMC is a dock management controller for docks, hubs, and monitors implementing TI PD
controllers. Suitable Power Delivery (PD) Controller devices include TPS6598x which are updated as
part of the DMC firmware. There may be multiple PD devices attached to each DMC device.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob as a SHA-256+RSA-3072
signed binary file.

This plugin supports the following protocol ID:

* com.ti.tps6598x

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_0451&PID_ACE1&REV_0001`
* `USB\VID_0451&PID_ACE1`
* `USB\VID_0451`

Devices also have additional instance IDs which corresponds to the UID, oUID and OTP config, e.g.

* `USB\VID_0451&PID_ACE1&UID_50bf5616c608a6b98b4169b220d9a5b8`
* `USB\VID_0451&PID_ACE1&OUID_2200000000000000`
* `USB\VID_0451&PID_0451&CONFIG_00000000A206000030000000`

## Update Behavior

The device usually presents in runtime mode.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0451`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
