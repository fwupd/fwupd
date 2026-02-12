---
title: Plugin: Sunwinon HID
---

## Introduction

This plugin updates Sunwinon HID devices (e.g. stylus/digitizer class) using a
vendor-specific DFU-like protocol transported over hidraw channel. This protocol is
derived from an update procedure originally designed by GOODIX.

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `HIDRAW\VEN_17EF&DEV_62CE`

## Firmware Format

Only unsigned firmware images are currently supported. Layout is as follows:

|Content|Size|
|-------|----|
|firmware binary data|variable, aligned to 16 bytes|
|image info block|40 bytes|
|reserved|8 bytes|

The structure of trailing image info block is defined in `fu-sunwinon-hid.rs` as
`FuStructSunwinonDfuImageInfo`.

## Protocol ID

This plugin registers the protocol ID `com.sunwinon.hid`.

## Update Behavior

The device will enter firmware update mode when receiving `ProgramStart` command,
and exit when receiving `ProgramEnd` command or process failed/timeout in the middle.

Commands during handshake are sent under normal runtime mode.

Device will replug then reboot into normal runtime mode automatically after procedure finished.
Due to a quirk in the device that it goes into unexpected state if message of HID layer is sent
between BLE 'Connected' and 'ServiceResolved', a 2000ms delay is added during setup.

## External Interface Access

Requires hidraw read/write access to send and receive HID reports.

## Vendor ID Security

The vendor ID is set from the HID vendor ID, in this instance set to `HIDRAW:0x17EF`
