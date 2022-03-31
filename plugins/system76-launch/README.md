# System76 Launch

## Introduction

This plugin is used to detach the System76 Launch device to DFU mode.

To switch to bootloader mode a USB packet must be written, as specified by the
System76 EC protocol.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_3384&PID_0001&REV_0001`
* `USB\VID_3384&PID_0005&REV_0001`

## Update Behavior

The device usually presents in runtime mode, but on detach re-enumerates with a
different USB VID and PID in DFU mode. The device is then handled by the `dfu`
plugin.

On DFU attach the device again re-enumerates back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x3384`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
