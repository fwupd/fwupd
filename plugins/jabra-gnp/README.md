# Jabra GNP

## Introduction

This plugin is used to firmware update for some Jabra devices
(refer to the `jabra-gnp.quirk` file for more information).
Notably this excludes devices supported by the `jabra` plugin,
as well as 1st edition Jabra Evolve (non-SE) devices and the
corresponding Jabra Link connectors.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_0B0E&PID_24DB`

## Update Behavior

The device is updated at runtime using USB control and interrupt transfers.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0B0E`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### JabraGnpAddress

Address to use, defaulting to `0x08`.

Since: 1.9.25

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.9.2`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Gianmarco: @gdpcastro
