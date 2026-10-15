---
title: Plugin: Realtek eDP Scaler
---

## Introduction

The Realtek eDP Scaler is the scaler chip inside an integrated (AIO) display.
It exposes a firmware-update ISP protocol over two independent links:

* **Identity** — the panel EDID (read by the kernel DRM subsystem), which fwupd
  turns into a `DRM\VEN_<PNP>&DEV_<product>` instance ID.
* **Update channel** — a pure SMBus link behind the PCH i801 adapter, exposed
  by the kernel as `/dev/i2c-N` (the bus number varies between boots, so the
  plugin locates the adapter by its sysfs name `SMBus I801 adapter`).

There is no kernel i2c client node for this device; the SMBus transfers are
done with the `I2C_SMBUS` ioctl on `/dev/i2c-N`.

Only the Intel PCH SMBus controller (the `i801` adapter) is currently supported.

## Firmware Format

The daemon will decompress the cabinet archive and extract a raw binary (no
header) which is streamed to the device in SMBus block chunks. The last byte of
the image is a CRC, which the scaler verifies itself once the write has
finished.

This plugin supports the following protocol ID:

* `com.realtek.edp-scaler`

## GUID Generation

These devices use the standard `DRM` DeviceInstanceId values, e.g.

* `DRM\VEN_RTK&DEV_01BF` → `5eedd707-3db7-5b40-9cf2-ec87de9d8ea9`

## Update Behavior

The firmware is written using the Realtek foreground ISP flow over SMBus. The
device is switched into foreground update mode, the project ID in the image is
verified by the device, and the image is streamed in 32-byte SMBus blocks. If
the device rejects the project ID, the update is aborted before anything is
written. The device is then reset to boot the new firmware.

The running version can only be read in foreground mode, so it is read by
briefly entering and leaving that mode on setup and again on reload.

## Vendor ID Security

The vendor ID is set from the EDID PNP ID, `PNP:RTK`.

## Quirk Use

This plugin does not use any plugin-specific quirks.

## External Interface Access

This plugin requires read/write access to the PCH SMBus adapter device file
(`/dev/i2c-N`), which the fwupd daemon normally has as root. The `i2c-dev` kernel
module must be loaded.

## Version Considerations

This plugin has been available since fwupd version `2.2.2`.
