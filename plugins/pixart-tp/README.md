---
title: Plugin: PixArt Touchpad
---

## Introduction

The PixArt Touchpad plugin (`pixart_tp`) updates PixArt touchpad firmware
that enumerates as HID devices (either USB-HID or I²C-HID exposed via `hidraw`).

The device can enter a lightweight bootloader (“engineer mode”) and is then flashed over the same
HID interface.

This plugin sets the fwupd device protocol to `com.pixart.tp` and reports version numbers in hex format.

## Firmware Format

The firmware is parsed by `FuPxiTpFirmware` and validated using:

- **Magic** `FWHD` (header v1.0)
- **Header CRC32** (over the header minus CRC field)
- **Payload CRC32** (over the bytes after the header)

Each updateable **internal** section defines a flash start address and a file offset/length.
The plugin programs flash in **4 KiB sectors** with **256-byte pages** via a small SRAM window.

This plugin supports the following protocol ID:

- `com.pixart.tp`

## GUID Generation

These devices use the standard HID DeviceInstanceId values, e.g.

- `HIDRAW\VEN_093A&DEV_0343`

> Note: If the same silicon enumerates as a USB interface on some systems,
> an additional USB GUID like `USB\VID_093A&PID_0343` may also be provided
> in the firmware metadata. GUIDs derived from hardware IDs are **stable
> across machines**.

## Update Behavior

High-level flow:

1. **Detach (enter bootloader)**
   Writes device registers to switch to engineer mode:
   - `bank 0x01, reg 0x2c = 0xaa`
   - `bank 0x01, reg 0x2d = 0xcc`

2. **Erase/Program**
   - Erase flash by **sector (4 KiB)**
   - Program by **page (256 B)** using an SRAM selected by `SramSelect`
   - Busy/write-enable checks are performed between operations

3. **Attach (exit bootloader)**
   - `bank 0x01, reg 0x2c = 0xaa`
   - `bank 0x01, reg 0x2d = 0xbb`

OS reboot is required.

## Quirk Use

This plugin supports the following plugin-specific quirks:

### `PxiTpHidVersionBank`

Defines which bank to read the device firmware version from.
**Default**: `0x00`.

### `PxiTpHidVersionAddr`

Defines which address to read the device firmware version from; the plugin reads `addr+0` (lo) and `addr+1` (hi).
**Default**: `0x0b`.

### `SramSelect`

Selects the SRAM type used for 256-byte page programming.
**Default**: `0x0f`.

## Vendor ID Security

The vendor ID is set from the HID vendor, in this instance set to `HIDRAW:0x093A`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.19`.
