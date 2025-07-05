---
title: Plugin: Synaptics VMM9xxxx
---

## Introduction

This plugin updates the VMM9xxxx MST devices from Synaptics.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.synaptics.mst-hid`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_06CB&PID_9430`

These devices also use custom GUID values using the board ID and customer ID values which are
different for each customer and hardware design, e.g.

* `USB\VID_06CB&PID_9430&BID_56`
* `USB\VID_06CB&PID_9430&BID_56&CID_78`

## Update Behavior

The device storage banks are erased, and each block from the firmware is written and then finally
activated. Calculating the CRC manually is no longer required.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x06CB` and also from the
customer ID (if set), e.g. `SYNA:0x12`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### Flags=manual-restart-required

The device is not capable of self-rebooting, and we have to ask the user to replug the power cable.

Since: 1.9.20

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.9.20`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Apollo Ling: @ApolloLing
