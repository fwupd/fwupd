---
title: Plugin: Sunwinon HID
---

## Introduction

This plugin updates Sunwinon HID devices (e.g. stylus/digitizer class) using a
vendor-specific DFU protocol transported over a hidraw channel.

## GUID Generation

The HID DeviceInstanceId from hidraw is used to build the primary GUID, e.g.
`HIDRAW\VEN_04F7&DEV_1234` or `HIDRAW\VEN_17EF&DEV_62CC` (see the bundled
`sunwinon-hid.quirk`).

## Firmware Format

- For now, all firmware images are unsigned and unencrypted. Layout is as follows:

|Content|size|
|-------|----|
|Firmware binary data|variable, aligned to 16 bytes|
|Image info block|40 bytes|
|reserved|8 bytes|

- The structure of trailing image info block is defined in `fu-sunwinon-util-dfu-master.h`
 as `FuSunwinonDfuImageInfo`.

## Protocol ID

This plugin registers the protocol ID `com.sunwinon.hid`.

## Update Behavior

- Uses hidraw report ID 0x61 with 480-byte reports (464-byte payload chunks).
- Handshake: `GetInfo` → `SystemInfo` → `FwInfoGet` → pre-check → `ModeSet`.
- Programming: `ProgramStart` → repeated `ProgramFlash` chunks → `ProgramEnd`.
- Fast DFU mode is currently not supported; the normal path is always used.
- The device is expected to stay on the same hidraw path (no replug cycle).

## External Interface Access

Requires hidraw read/write access to send and receive HID feature/output
reports.

## Security Notes

- The plugin sets `FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD`; unsigned blobs are
 permitted. Signed images are optionally detected via the trailing signature
 block.
- GUIDs are tied to the hidraw VID/PID to scope firmware delivery.
