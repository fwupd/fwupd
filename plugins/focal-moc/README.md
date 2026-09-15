---
title: Plugin: FocalTech Fingerprint Sensor
---

## Introduction

This plugin updates firmware for FocalTech Match-On-Chip (MOC) fingerprint sensors over USB bulk
transfer.

The communication protocol is derived from the reference `UpgradeTool` source code provided by
FocalTech Systems Co., Ltd.

Devices use one of two generations of the vendor firmware-update protocol.  The default device
type detaches with plaintext runtime commands and retains the runtime PID in IAP mode.  Newer
devices, matched to `FuFocalMocSignedDevice` in `focal-moc.quirk`, require an encrypted
TransportSec channel for runtime commands and re-enumerate the IAP at PID `0x0201`.

## Firmware Format

The daemon decompresses the cabinet archive and extracts a flat binary firmware blob.
No additional wrapping is expected.  The plugin computes a CRC32 over the full image, sends it in
the SOH header, then streams the image in 1 KiB STX blocks followed by a zero-padded EOT block for
any remainder.  The update stream is limited to 255 1 KiB data frames because the protocol uses an
8-bit frame number.

Newer devices instead use a 256-byte `FWHD` header followed by the application body.  The magic
0x46574844 ("FWHD") is stored little-endian, so the file starts with the raw bytes `DHWF`.  The
header identifies an APP image, supplies the monotonic version, and contains the body digest and
ECDSA-P256 signature.  The complete signed image is streamed using 16-bit frame numbers, and the
IAP checks the image CRC and signature before booting it; if verification fails and the previous
application is valid, the IAP restores that backup.

This plugin supports the following protocol ID:

* `com.focal.moc`

## GUID Generation

These devices use the standard USB instance ID values such as `USB\VID_2808&PID_XXXX`.

## Update Behavior

The firmware is deployed when the device is in normal runtime mode.
The plugin first switches the device to bootloader (IAP) mode (`detach`), then streams the firmware
image using the 3-phase SOH/STX/EOT protocol.
The device reboots automatically after the final ACK; the plugin then issues a return-to-app
command (`attach`) in case the device has not yet rebooted.
The device re-enumerates after each mode switch.

Before the transfer the wake-up command confirms the device is responsive rather than discovering
a dead device part way through the frames.  A bootloader is probed once to tell its legacy status
codes apart from the unified ones, so an ambiguous failure is not reported as though it had a
known meaning.  Reply parsing locates the checksum from the declared length, allowing only the
padding the firmware appends past the declared frame.

Default devices use the same USB product ID in both modes, so the mode is taken from the `_APP_`
or `_IAP_` marker in the version string reported by the device.
A device left in bootloader mode by an interrupted update is detected as such, and an update can
start without another mode switch.

Newer devices establish an ephemeral ECDH P-256 TransportSec session before any runtime command:
the handshake derives direction-specific AES-256-CTR, HMAC-SHA256, and IV keys and confirms them,
and every command then travels in an authenticated cipher frame with strict sequence numbers and
fragment reassembly.  The runtime detach command is encrypted; the `0x0201` IAP then accepts the
signed `FWHD` image in plaintext, so a device stuck in IAP stays recoverable.

TransportSec requires GnuTLS 3.8.2 or later.  Builds using an older GnuTLS keep these runtime
devices visible but not updatable.

## Vendor ID Security

The vendor ID is set from the USB vendor ID, in this instance `USB:0x2808`.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.1.8`.
