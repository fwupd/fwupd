---
title: Plugin: Aver HID-ISP
---

## Introduction

The AVer HID In-System-Programming plugin is used for various products that can be updated using
a proprietary HID protocol.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.aver.hid`

## GUID Generation

These devices use the standard DeviceInstanceId values, e.g.

* `USB\VID_2574&PID_09F0`

## Update Behavior

The device is updated using a HID request/response with a fixed size payload.

Once ready, the plugin can start the update with the `UVCX_UCAM_ISP_FILE_START` header.
After the device sends back the `UVCX_UCAM_ISP_FILE_START` packet, the PC process can send the
firmware file in chunks using `UVCX_UCAM_ISP_FILE_DNLOAD`.
After the last chunk, the plugin sends `UVCX_UCAM_ISP_FILE_END` packet and the device will check
whether the firmware is valid. If the firmware file is correct, the device will send
`UVCX_UCAM_ISP_START` to PC, and the plugin can continuously send `UVCX_UCAM_ISP_STATUS` to get
the ISP progress percentage.

If the firmware file is incorrect, the device sends `UVCX_UCAM_ISP_STOP` back to the plugin,
and the ISP progress should be terminated. The PC process should go back to `UVCX_UCAM_ISP_STATUS`
and restart the process if needed.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x2574`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.9.2`.
