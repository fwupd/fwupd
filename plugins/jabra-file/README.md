# Jabra FILE

## Introduction

This plugin is used to firmware update the Jabra PanaCast50 and the Lenovo ThinkSmart Bar 180.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_0B0E&PID_3011`

## Update Behavior

The device is updated at runtime using USB interrupt transfers.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0B0E`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Gianmarco: @gdpcastro
