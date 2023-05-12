---
title: Plugin: UEFI dbx
---

## Introduction

Updating the UEFI revocation database prevents starting EFI binaries with known
security issues, and is typically no longer done from a firmware update due to
the risk of the machine being "bricked" if the bootloader is not updated first.

This plugin also checks if the UEFI dbx contains all the most recent revoked
checksums. The result will be stored in an security attribute for HSI.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
EFI_SIGNATURE_LIST format.

See <https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf>
for details.

This plugin supports the following protocol ID:

* `org.uefi.dbx`

## GUID Generation

These devices use the GUID constructed of the uppercase SHA256 of the X509
certificates found in the system KEK and optionally the EFI architecture. e.g.

* `UEFI\CRT_{sha256}`
* `UEFI\CRT_{sha256}&ARCH_{arch}`

...where `arch` is typically one of `IA32`, `X64`, `ARM` or `AA64`

## Metadata

Microsoft actually removes checksums in some UEFI dbx updates, which is probably a result of OEM
pressure about SPI usage -- but local dbx updates are append-only.
This means that if you remove hashes then you can have a different number of dbx checksums on your
machine depending on whether you went `A→B→C→D` or `A→D`...
As we count the number of SHA256 checksums to build the *dbx version number* then we might overcount
(due to the now-removed entries) -- in some cases enough to not actually apply the new update at all.

In these cases we look at the *last-entry* dbx checksum and compare to the set we know, to see if
fwupd needs to artificially lower the reported version.
This dbx *last-entry* checksum is added to the LVFS metadata and is copied to the device object
when the exact checksum matches.
This ensures the locally reported version number always matches what a factory install using just
the new dbx would report.

The `org.linuxfoundation.dbx.*.firmware` components will match against a hash of the system PK.
The latest cabinet archive can also be installed into the `vendor-firmware`
remote found in `/usr/share/fwupd/remotes.d/vendor/firmware/` which allows the version-fixup to work
even when offline -- although using the LVFS source is recommended for most users.

Both reported versions and *last-entry checksums* can be found from the
`fwupdtool firmware-parse DBXUpdate-$VERSION$.x64.bin efi-signature-list` command.

## Update Behavior

The firmware is deployed when the machine is in normal runtime mode, but it is
only activated when the system is restarted.

## Vendor ID Security

The vendor ID is hardcoded to `UEFI:Microsoft` for all devices.

## External Interface Access

This plugin requires:

* read/write access to `/sys/firmware/efi/efivars`

## Version Considerations

This plugin has been available since fwupd version `1.5.0`.
