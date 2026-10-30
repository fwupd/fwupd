---
title: Plugin: Realtek Scaler
---

## Introduction

The Realtek Scaler is the display scaler chip found in all-in-one (AIO) and
external displays.
It exposes a firmware-update ISP protocol over I²C, via the DDC channel of the
display connector (the DisplayPort aux channel for eDP and DP connectors).

* **Identity** — the display EDID (read by the kernel DRM subsystem), which fwupd
  turns into a `DRM\VEN_<PNP>&DEV_<product>` instance ID. Each display model
  is enabled by adding its instance ID to `realtek-scaler.quirk`.
* **Update channel** — the I²C adapter linked from the DRM connector (the `ddc`
  symlink in sysfs), which fwupd opens as the proxy device.

The scaler answers on two I²C addresses: `0x37` for DDC/CI and `0x1D` for the
foreground ISP interface. The address is selected before every transfer.

## Firmware Format

The daemon will decompress the cabinet archive and extract a raw binary (no
header) which is streamed to the device in 32-byte I²C chunks. The last byte of
the image is a CRC, which the scaler verifies itself once the write has
finished.

This plugin supports the following protocol ID:

* `com.realtek.scaler`

## GUID Generation

These devices use the standard `DRM` DeviceInstanceId values, e.g.

* `DRM\VEN_DEL&DEV_C927` → `f4e24e2d-5e5e-586d-b89f-90f4005ce80e`

## Update Behavior

The firmware is written using the Realtek foreground ISP flow over I²C. The
device is switched into foreground update mode, the project ID in the image is
verified by the device, and the image is streamed in 32-byte blocks. If
the device rejects the project ID, the update is aborted before anything is
written. The device is then reset to boot the new firmware.

The running version can only be read in foreground mode, so it is read by
briefly entering and leaving that mode on setup and again on reload.

## Vendor ID Security

The vendor ID is set from the EDID PNP ID of the display, e.g. `PNP:DEL`.

## Quirk Use

This plugin does not use any plugin-specific quirks.

## External Interface Access

This plugin requires read/write access to the I²C adapter of the display
connector (`/dev/i2c-N`), which the fwupd daemon normally has as root. The `i2c-dev`
kernel module must be loaded.

## Version Considerations

This plugin has been available since fwupd version `2.2.2`.
