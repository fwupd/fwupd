---
title: Plugin: DFU
---

## Introduction

Device Firmware Update is a standard that allows USB devices to be easily and
safely updated by any operating system.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
DFU or DfuSe file format.

This plugin supports the following protocol IDs:

* `org.usb.dfu`
* `com.st.dfuse`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_273F&PID_1003&REV_0001`
* `USB\VID_273F&PID_1003`

## Update Behavior

A DFU device usually presents in runtime mode (with optional DFU runtime), but
on detach re-enumerates with an additional required DFU descriptor. On attach
the device again re-enumerates back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

## Implementation Notes

The runtime mode is just as important as the DFU mode from the point of view of
fwupd and should be included if firmware updates are to "just work". Without a
DFU runtime interface we can match the device with `Flags = no-dfu-runtime` but
will need a suitably new fwupd version before the device is recognized.

The USB interface revision (`REV`) is used as the BCD version number, as DFU has
no way of representing a firmware version number. A new firmware version should
always increment the USB REV of the *runtime* interface as fwupd will **not** switch
the device into *DFU mode* during enumeration to read the version number.
The version number of the DFU mode should represent the *bootloader version* and
this should not change as the firmware is updated.

The runtime USB interface should have a unique vendor ID and product ID for the
specific firmware stream. A different version of software should have a unique
VID/PID USB descriptor pair. The microcontroller example VID/PID should **never**
be used in the runtime mode otherwise fwupd would not know what firmware to match.

Ideally, the bootloader should also have a unique USB vendor ID and product ID.
This allows fwupd to more easily recognize the runtime interface *going away* and
the DFU interface *coming back*. If the VID/PID is the same in runtime and DFU
modes then the quirk `Flags = no-pid-change` is required.

If the bootloader VID/PID is not customized (as might be the default for the supplied
MCU) then fwupd can match the runtime VID/PID to the bootloader VID/PID. Where this
fails is when the device is *stuck* in the DFU mode, perhaps because the user removed
the USB cable before the device had completed updating.
With a unique VID/PID fwupd can *recover* the device stuck in DFU mode, reflashing the
device with the latest matching firmware and then attaching it back into runtime mode.

Using a *generic* VID/PID for the bootloader means fwupd does not know how to recover
the device back into runtime mode as the client does not know what firmware to choose
and the user is forced to either RMA the device, or to download the correct file manually
from the vendor vebsite and use low-level commands like `sudo fwupdtool install-blob`.

## Vendor ID Security

The vendor ID is set from the USB vendor, for example `USB:0x0A12`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### DfuFlags

Optional quirks for a DFU device which doesn't follow the DFU 1.0 or 1.1 specification.

Since: 1.0.1

### DfuForceVersion

Forces a specific DFU version for the hardware device. This is required if the device does not set, or sets incorrectly, items in the DFU functional descriptor.
If set to 0000 then the DFU functionality is disabled.

Since: 1.0.1

### DfuForceTimeout

Forces a specific device timeout, in ms.

Since: 1.4.0

### DfuForceTransferSize

Forces a target transfer size, in bytes.

Since: 1.5.6

### `Flags=can-download`

Can download from host->device.

### `Flags=can-upload`

Can upload from device->host.

### `Flags=manifest-tol`

Can answer GetStatus in manifest

### `Flags=will-detach`

Will self-detach.

### `Flags=can-accelerate`

Use a larger transfer size for speed.

### `Flags=attach-upload-download`

An upload or download is required for attach.

### `Flags=force-dfu-mode`

Force DFU mode.

### `Flags=ignore-polltimeout`

Ignore the device download timeout.

### `Flags=ignore-runtime`

Device has broken DFU runtime support.

### `Flags=ignore-upload`

Uploading from the device is broken.

### `Flags=no-dfu-runtime`

No DFU runtime interface is provided.

### `Flags=no-get-status-upload`

Do not do GetStatus when uploading.

### `Flags=no-pid-change`

Accept the same VID:PID when changing modes.

### `Flags=use-any-interface`

Use any interface for DFU.

### `Flags=use-atmel-avr`

Device uses the ATMEL bootloader.

### `Flags=use-protocol-zero`

Fix up the protocol number.

### `Flags=legacy-protocol`

Use a legacy protocol version.

### `Flags=detach-for-attach`

Requires a FU_DFU_REQUEST_DETACH to attach.

### `Flags=absent-sector-size`

In absence of sector size, assume byte.

### `Flags=manifest-poll`

Requires polling via GetStatus in dfuManifest state.

### `Flags=no-bus-reset-attach`

Do not require a bus reset to attach to normal.

### `Flags=gd32`

Uses the slightly weird GD32 variant of DFU.

### `Flags=allow-zero-polltimeout`

Allows the zero bwPollTimeout from GetStatus in dfuDNLOAD-SYNC state.

### `Flags=index-force-detach`

Requires Force Detach in wIndex to bypass status checking.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `0.8.0`.
