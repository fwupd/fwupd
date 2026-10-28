---
title: Plugin: XY KBMS
---

## Introduction

This plugin supports the XY KBMS controller boards used to convert ThinkPad keyboards to USB.

The board has two updatable parts:

* the app firmware, e.g. `xykbms_app_release.bin`
* the pinmap, e.g. `xykbms_pin_usb_default_uk.cfg`, which describes how the keyboard matrix is
  wired and which key codes are sent

## Firmware Format

The app firmware is an encrypted binary image with no header, which is decrypted by the
bootloader. The image size must be a multiple of 2048 bytes.

The pinmap is a text file of `0xNN` bytes and little-endian `0xNNNN` words, where anything
after `;` is a comment. It parses to a 968 byte block that is merged into the existing 1024 byte
config block, so that other settings such as the Bluetooth name are not changed.

This plugin supports the following protocol IDs:

* `com.xy.kbms` for the app firmware
* `com.xy.kbms.config` for the pinmap

## GUID Generation

These devices use the standard HIDRAW DeviceInstanceId values, e.g.

* `HIDRAW\VEN_5859&DEV_0001`

The pinmap child device uses an additional instance ID:

* `HIDRAW\VEN_5859&DEV_0001&PART_PINMAP`

## Update Behavior

All commands use HID output report 2 on the vendor-defined interface, with the reply in input
report 2.

The pinmap is written by reading the current config block, replacing the pinmap and updating the
CRC. The keyboard resets itself after the config has been written, and the config is then read
back to verify it.

The app firmware is written in BOOT mode. The device does a USB reset when entering BOOT mode, but
does not re-enumerate and keeps the same VID and PID. In BOOT mode every request is treated as a
firmware block, so only the firmware is written, and the device stays in BOOT mode until it has
been written or is power cycled.

The firmware version is the date the firmware was built, e.g. `2023.06.15`.

## Vendor ID Security

The vendor ID is set from the HID vendor ID, in this instance set to `HIDRAW:0x5859`.

## External Interface Access

This plugin requires read/write access to `/dev/hidraw*`.

## Version Considerations

This plugin has been available since fwupd version `2.2.1`.
