---
title: Plugin: LXS Touchscreen
---

## Introduction

This plugin allows updating the firmware on LXS Semiconductor touchscreen
devices. Devices are enumerated using HID (hidraw).

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary format. Two firmware image sizes are supported:

* `116 KiB` — application firmware only (written from flash offset `0x3000`)
* `128 KiB` — full firmware (bootloader + application, written from offset `0x0000`)

This plugin supports the following protocol ID:

* `com.lxsemicon.swip`

## GUID Generation

These devices use the standard DeviceInstanceId values derived from the USB
vendor and product IDs, e.g.

* `USB\VID_1FD2&PID_5010` — runtime (application) mode
* `USB\VID_1FD2&PID_B011` — bootloader mode (VID case 1)
* `USB\VID_29BD&PID_B011` — bootloader mode (VID case 2)
* `HIDRAW\VEN_1FD2&DEV_5010`

## Update Behavior

The device presents in runtime mode using the SWIP (SW Interface Protocol).
Firmware is written by first switching the device into DFUP (Device Firmware
Update Protocol) bootloader mode. The device re-enumerates and the firmware
is transferred in 128-byte blocks, each subdivided into 16-byte chunks written
to a parameter buffer register (`0x6000`), followed by an IAP (In-Application
Programming) flash write command (`0x1400`). After each block the plugin waits
for the device to report a ready status (`0xA0`) via the getter register
(`0x0600`). Once all blocks are written, the device is reset via a watchdog
trigger and re-enumerates back in application mode. The new firmware version
is then verified by re-reading the integrity register.

On failure during the flash write phase the device remains in bootloader mode
and can be recovered by re-running the firmware update.

## Vendor ID Security

The vendor ID is set from the USB vendor, for example `USB:0x1FD2`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

* `CounterpartGuid`: Links runtime and bootloader USB device instances so
  fwupd can match the device across the DFUP mode re-enumeration.

## External Interface Access

This plugin requires read/write access to the hidraw node for the device.
It uses raw `pread`/`pwrite` on the hidraw device node; no ioctl calls are
made directly.

## Version Considerations

This plugin has been available since fwupd version `2.1.1`.
