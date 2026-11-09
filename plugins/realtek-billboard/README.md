---
title: Plugin: Realtek Billboard
---

## Introduction

This plugin updates the firmware of Realtek USB Billboard Class devices.
The Billboard Class (USB class `0x11`) allows the device to expose its characteristics to the
operating system, while the vendor control transfers are used to program the on-board flash.

## Firmware Format

The daemon will decompress the cabinet archive and extract a raw binary firmware blob.
There is no specific header or container format; the payload is the flash contents of the device,
written in chunks of at most 256 bytes using vendor control transfers.

This plugin supports the following protocol ID:

* `com.realtek.billboard`

## LVFS-Protocol Commands

These commands are carried over vendor control transfers with `wIndex = 0x0000` and the opcode in
the low byte of `wValue`.
Every reply starts with a one byte `bRequest` echo, and all commands with an opcode repeat it as a
second echo byte.
A reply of `0x00` to a command that reports a result means failure, `0x01` means success.
Reset is the exception: it has no data stage and no reply, so the device reports failure with a
STALL on the status stage.

| bmRequestType | bRequest | wValue | wLength | Reply |
| --- | --- | --- | --- | --- |
| `0xC0` | `0x20` | `0x0000` | 3 | `0x20` + 2 byte FW version |
| `0xC0` | `0x21` | `0x0001` | 7 | `0x21` + `0x01` + 4 byte addr + 1 byte len |
| `0x40` | `0x21` | `0x0002` | len | Project ID to verify |
| `0xC0` | `0x21` | `0x0003` | 3 | `0x21` + `0x03` + 1 byte result |
| `0xC0` | `0x22` | `0x0001` | 3 | `0x22` + `0x01` + 1 byte result |
| `0x40` | `0x22` | `0x0002` | len | Firmware data |
| `0xC0` | `0x22` | `0x0003` | 3 | `0x22` + `0x03` + 1 byte result |
| `0x40` | `0x23` | `0x0000` | 0 | none, STALL means failure |

## GUID Generation

These devices use the standard USB Device InstanceId values, e.g.

* `USB\VID_32AC&PID_0029`

## Update Behavior

The update is a five step sequence:

1. `GetProjectIDAddressInfo` is used to find where the Project ID lives in the
   image, that region of the new image is sent with `SetVerifyProjectID`, and
   `GetVerifyProjectIDResult` confirms the device accepted it, so an image built
   for a different project is rejected before anything is written. The device
   reports the offset relative to the start of the image, not an absolute flash
   address, so it is used to seek directly into the payload.
2. `FW Update Start` erases the flash and gets the device ready for data.
3. The image is streamed with `FW Update Write data`, one 256 byte control
   transfer per packet.
4. `FW Update Finish` commits the image and makes the device check it.
5. `Reset` starts the device running the new image.

The billboard function keeps running while the flash is written, but `Reset` takes the device off
the bus, so the daemon waits for it to re-enumerate and only then reads the new version back.
The device must be plugged into a USB-C port that exposes the Billboard function.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x32AC`.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.2.1`.
