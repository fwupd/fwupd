---
title: Plugin: FocalTech MOC Fingerprint Sensor
---

## Introduction

This plugin updates the application firmware on FocalTech FT9001
match-on-chip fingerprint sensors. The update uses the device IAP and does not
modify the secure bootloader, IAP firmware, provisioning data, or fingerprint
templates.

Devices use one of two generations of the vendor firmware-update protocol.
Update protocol v1 uses plaintext runtime commands and retains the runtime PID
in IAP mode. Update protocol v2 requires an encrypted TransportSec channel to
enter IAP and re-enumerates the IAP at PID `0x0201`.

Supported devices are listed individually in `focal-moc.quirk`, which also
selects the protocol generation for each one.

## Firmware Format

Update protocol v1 uses the signed raw application image accepted by its IAP.
The update stream is limited to 255 1 KiB data frames because the protocol
uses an 8-bit frame number.

Update protocol v2 uses a 256-byte `FWHD` header followed by the application
body. The magic 0x46574844 ("FWHD") is stored little-endian, so the file starts
with the raw bytes `DHWF`. The header identifies an APP image, supplies the
monotonic version, and contains the body digest and ECDSA-P256 signature.

## Update Behavior

The plugin reads command `0x30` to obtain the firmware version and to identify
`_APP_` or `_IAP_` mode. A device left in bootloader mode by an interrupted
update is detected as such, and an update can start without another mode
switch. It sends `0x32 {0x01}` to detach a protocol v1 runtime
device into IAP, transfers the image with command `0x33` using SOH, STX, and EOT
frames, and sends `0x32 {0x00}` to verify, commit, and restart the application.

Update protocol v2 establishes an ephemeral ECDH P-256 TransportSec session,
derives direction-specific AES-256-CTR, HMAC-SHA256, and IV keys, and confirms
the keys before sending runtime commands. The session enforces authenticated
packet sequence numbers and reassembles fragmented responses. The runtime
detach command is encrypted; the `0x0201` IAP then accepts the signed `FWHD`
image using the protocol v2 YMODEM framing.

TransportSec requires GnuTLS 3.8.2 or later. Builds using an older GnuTLS keep
protocol v2 runtime devices visible but not updatable; a device already in IAP
mode remains recoverable because IAP transfer does not use TransportSec.

Update protocol v1 devices use a unified command status layout in current IAP
firmware. The plugin probes command `0x31` in IAP mode; older IAP firmware
remains updateable, but ambiguous failures are reported using their raw device
status. If the probe cannot classify the bootloader the update is stopped with
a clear error.

The IAP checks the image CRC and signature before booting it. If verification
fails and the previous application is valid, the IAP restores that backup.

This plugin supports the following protocol ID:

* `com.focal.moc`

## GUID Generation

Devices use the standard USB instance ID, for example
`USB\VID_2808&PID_A97A`. Firmware metadata must target the GUID produced from
the complete VID and PID, which is what keeps a metadata-driven install from
offering an image built for a different product or protocol generation. The
device itself authenticates only the image signature, so a manually supplied
blob is not subject to that check.

## Vendor ID Security

The vendor ID is set from the USB vendor ID, in this instance `USB:0x2808`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### Flags=update-protocol-v1

Device uses update protocol v1, which detaches with plaintext runtime commands
and retains the runtime PID in IAP mode.

Since: 2.2.2

### Flags=update-protocol-v2

Device uses update protocol v2, which requires an encrypted TransportSec
channel to enter IAP and re-enumerates the IAP at PID `0x0201`.

Since: 2.2.2

## External Interface Access

This plugin requires read and write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.1.8`.
