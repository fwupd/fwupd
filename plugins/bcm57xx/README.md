---
title: Plugin: BCM57xx
---

## Introduction

This plugin updates BCM57xx wired network adaptors from Broadcom using a
reverse-engineered flashing protocol. It is designed to be used with the
clean-room reimplementation of the BCM5719 firmware found here:
<https://github.com/meklort/bcm5719-fw>

## Protocol

BCM57xx devices support a custom `com.broadcom.bcm57xx` protocol which is
implemented as ioctls like ethtool does.

## GUID Generation

These devices use the standard PCI instance IDs, for example:

* `PCI\VEN_14E4&DEV_1657`
* `PCI\VEN_14E4&DEV_1657&SUBSYS_17AA222E`

## Update Behavior

The device usually presents in runtime mode, and the firmware is written to the
device without disconnecting the working kernel driver. Once complete the APE
is reset which may cause a brief link reconnection.

## Vendor ID Security

The vendor ID is set from the PCI vendor, in this instance set to `PCI:0x14E4`

## External Interface Access

This plugin requires the `SIOCETHTOOL` ioctl interface.

## Version Considerations

This plugin has been available since fwupd version `1.5.0`.
