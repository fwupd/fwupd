# DFU

## Introduction

Device Firmware Update is a standard that allows USB devices to be easily and
safely updated by any operating system.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
DFU or DfuSe file format.

This plugin supports the following protocol IDs:

* org.usb.dfu
* com.st.dfuse

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_273F&PID_1003&REV_0001`
* `USB\VID_273F&PID_1003`
* `USB\VID_273F`

## Update Behavior

A DFU device usually presents in runtime mode (with optional DFU runtime), but
on detach re-enumerates with an additional required DFU descriptor. On attach
the device again re-enumerates back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

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

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
