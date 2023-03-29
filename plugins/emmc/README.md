---
title: Plugin: eMMC
---

## Introduction

This plugin reads the sysfs attributes corresponding to eMMC devices.
It uses the kernel MMC API for flashing devices.

## Protocol

eMMC devices support the `org.jedec.mmc` protocol.

## GUID Generation

These devices use the following instance values:

* `EMMC\NAME_%name%`
* `EMMC\NAME_%name%&REV_%rev%`
* `EMMC\MAN_%manfid%&OEM_%oemid%`
* `EMMC\MAN_%manfid%&OEM_%oemid%&NAME_%name%`
* `EMMC\MAN_%manfid%&NAME_%name%&REV_%rev%`
* `EMMC\MAN_%manfid%&OEM_%oemid%&NAME_%name%&REV_%rev%`

One deprecated instance ID is also added; new firmware should not use this.

* `EMMC\%manfid%&%oemid%&%name%`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, but it is
only activated when the device is rebooted.

## Vendor ID Security

The vendor ID is set from the EMMC vendor, for example set to `EMMC:{$manfid}`

## External Interface Access

This plugin requires ioctl `MMC_IOC_CMD` and `MMC_IOC_MULTI_CMD` access.

## Version Considerations

This plugin has been available since fwupd version `1.3.3`.
