---
title: Plugin: Corsair
---

## Introduction

This plugin allows to update firmware on Corsair mice and receivers:

* SABRE RGB PRO WIRELESS
* SLIPSTREAM WIRELESS USB Receiver
* KATAR PRO WIRELESS
* KATAR PRO XT Gaming Mouse
* SABRE PRO Gaming Mouse

## Code structure

All devices handled by one object (FuCorsairDevice). Receivers with wireless-only
devices will be shown as two entities: parent device as a receiver and wireless
device as a child. Difference in behavior is handled by private flags.

## Wired mice update behavior

Mice and/or it's wireless adapter must be connected to host via USB cable
to apply an update. The device is switched to bootloader mode to flash
updates, and is reset automatically to new firmware after flashing.

## Wireless mice update behavior

The receiver should be connected to host and the mouse should be turned on
and not sleeping.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1B1C&PID_1B94`

Additionally, an extra instance ID is used for the wireless subdevice, e.g.

* `USB\VID_1B1C&PID_1B94&DEV_MOUSE`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### CorsairVendorInterfaceId

Some devices have non-standard USB interface for protocol communication.
This quirk should be set if protocol interface is not 1.

Since: 1.8.0

### Flags:is-receiver

This flag is used if the device is a wireless receiver.

Since: 2.1.1

### Flags:legacy-attach

This flag is used if legacy attach command should be used.

## Version Considerations

This plugin has been available since fwupd version `1.8.0`.
