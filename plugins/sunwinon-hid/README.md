---
title: Plugin: Sunwinon HID
---

## Introduction

This plugin updates Sunwinon HID devices (e.g. stylus/digitizer class) using a
vendor-specific DFU-like protocol transported over hidraw channel. This protocol is
derived from an update procedure originally designed by GOODIX. See section
`License Notes` for details.

## GUID Generation

The HID DeviceInstanceId from hidraw is used to build GUID, e.g.
 `HIDRAW\VEN_17EF&DEV_62CC`. See `sunwinon-hid/quirk`.

## Firmware Format

For now, all firmware images are unsigned and unencrypted. Layout is as follows:

|Content|Size|
|-------|----|
|firmware binary data|variable, aligned to 16 bytes|
|image info block|40 bytes|
|reserved|8 bytes|

The structure of trailing image info block is defined in `fu-sunwinon-util-dfu-master.h`
 as `FuSunwinonDfuImageInfo`.

## Protocol ID

This plugin registers the protocol ID `com.sunwinon.hid`.

## Update Behavior

The device will enter firmware update mode when receiving `ProgramStart` command,
and exit when receiving `ProgramEnd` command or process failed/timeout in the middle.

Commands during handshake are sent under normal runtime mode.

No replug or re-enumeration is needed during the update process. Device will
reboot into normal runtime mode automatically after procedure finished.

## External Interface Access

Requires hidraw read/write access to send and receive HID reports.

## Vendor ID Security

The vendor ID is set from the HID vendor ID, in this instance set to `HIDRAW:0x17EF`

## Security Notes

The plugin currently accepts unsigned and unencrypted firmware images only.

## License Notes

`fu-sunwinon-util-dfu-master.c` and `fu-sunwinon-util-dfu-master.h` are derived
from GOODIX's `dfu_master.c` and `dfu_master.h` files, which are part of
[GOODIX GR551x SDK](https://github.com/goodix-ble/GR551x.SDK)
that originally licensed under the BSD-3-Clause License. The derived code
is dual-licensed under both the BSD and LGPLv2.1+ licenses.
See `GOODIX-BSD-LICENSE` for original license.
