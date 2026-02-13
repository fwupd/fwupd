---
title: Plugin: Himax Touchscreen Controller
---

## Introduction

This plugin is used for updating firmware on Himax Touchscreen device.
Devices are enumerated using HID.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `tw.com.himax.tp`

## GUID Generation

These device uses the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_3558&DEV_14FD`

Also, it may specify customer/project ID by CID, e.g.

* `HIDRAW\VEN_3558&DEV_14FD&CID_21`

## Update Behavior

The device support runtime firmware update and do not need to boot into bootloader
in the process. Device will reboot itself when update is complete, and need sometime
to get ready. The report descriptor may change after update, so system reboot is
preferred to reload the HID.

## Vendor ID Security

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x3558`

## External Interface Access

This plugin requires ioctl access to `HIDIOCSFEATURE` and `HIDIOCGFEATURE`.

## Version Considerations

This plugin has been available since fwupd version `2.1.1`.
