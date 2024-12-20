---
title: Plugin: qc-s5gen2
---

## Introduction

Firmware Update Plug-in for Qualcomm Voice & Music Series 5 Gen 1 and Gen 2, and Series 3 Gen 1, Gen 2 and Gen 3.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

The DFU file format is covered in documentation from Qualcomm, referenced by 80-CH281-1.

This plugin supports the following protocol ID:

* `com.qualcomm.s5gen2`

## GUID Generation

These devices use the standard  DeviceInstanceId values, e.g.

* `USB\VID_0A12&PID_4007`

Pair 0A12:4007 is shared among vendors and shouldn't be used for the single device.
Also these devices use a custom GUID generation scheme, please use GUID based on
manufacturer and product names in case of shared VID:PID pair:

* `USB\VID_0A12&PID_4007&MANUFACTURER_{iManufacturer}&PRODUCT_{iProduct}`

Typically, BlueTooth devices should be detected by GAIA primary service with if
default vendor ID has been used:

* `BLUETOOTH\GATT_00001100-d102-11e1-9b23-00025b00a5a5`

For firmware file, it is recommended to use the unique GUID generated from the
variant read from the device, for instance:

* `BLUETOOTH\GAIA_QCC5171`

If needed to use own vendor ID for communication, the name detected by BlueZ
backend should be used in quirk file:

* `BLUETOOTH\NAME_QCC5171`

## Update Behavior

The device is updated in runtime mode and rebooted with a new version.
The commit command should be used after the update process is done, otherwise
the device will reboot with the previous firmware version.

The upgrade protocol and update behivior are specified in documentation from Qualcomm,
referenced by 80-CH281-1 and 80-CU043-1.

It is expected that OS is responsible for the correct BLE reconnection during update
of BlueTooth devices.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0A12`

## Quirk Use

This plugin uses the following plugin-specific quirks:

* no specific quirks

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.9.16`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Denis Pynkin: @d4s
