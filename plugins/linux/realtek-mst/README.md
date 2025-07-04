---
title: Plugin: Realtek MST
---

## Introduction

This plugin updates the firmware of DisplayPort MST hub devices made by Realtek,
such as the RTD2141b and RTD2142.

These devices communicate over I²C, via the DisplayPort aux channel. Devices
are declared by system firmware, and quirks specify the aux channel to which
the device is connected for a given system.

System firmware must specify the device's presence because while they can be
identified partially through the presence of Realtek's OUI in the Branch
Device OUI fields of DPCD (DisplayPort Configuration Data), they do not have
unique Device Identification strings.

This plugin was neither written, verified, supported or endorsed by Realtek
Semiconductor Corp.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is written to the partition of the
device flash that is not currently running.

This plugin supports the following protocol ID:

* `com.realtek.rtd2142`

## GUID Generation

These devices use the standard I2C DeviceInstanceId values, e.g.

* `I2C\NAME_1AF80175:00`

In the case where the I2C name is generic, we can also use a per-system HWID value, for example:

* `[I2C\NAME_AUX-C-DDI-C-PHY-C&HWID_9c908a5c-090e-5eb4-a7ba-a2ef8845a6b9]`

## Vendor ID security

The vendor ID is specified by system firmware (such as ACPI tables).

## External Interface Access

This plugin requires access to i2c buses associated with the specified
DisplayPort aux channel, usually `/dev/i2c-5` or similar.

## Version Considerations

This plugin has been available since fwupd version `1.6.2`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Peter Marheine: @tari
