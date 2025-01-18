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

* `org.uefi.dbx2`

## Comparing Versions

Earlier versions of fwupd used the `org.uefi.dbx` protocol which counted the number of Microsoft
hashes in the local `dbx` file and in the firmware payload.

The equivalent versions are given below:

### x64

| Old Version | New Version |
|-------------|-------------|
| `9`         | `20100307`  |
| `13`        | `20140413`  |
| `77`        | `20160809`  |
| `190`       | `20200701`  |
| `211`       | `20210401`  |
| `217`       | `20220801`  |
| `220`       | `20230301`  |
| `371`       | `20230501`  |

### aarch64

| Old Version | New Version |
|-------------|-------------|
| `19`        | `20200729`  |
| `21`        | `20210401`  |
| `22`        | `20220801`  |
| `26`        | `20230501`  |

### i386

| Old Version | New Version |
|-------------|-------------|
| `41`        | `20200701`  |
| `55`        | `20210401`  |
| `57`        | `20230301`  |
| `89`        | `20230501`  |

## GUID Generation

These devices use the GUID constructed of the uppercase SHA256 of the X509
certificates found in the system KEK and optionally the EFI architecture. e.g.

* `UEFI\CRT_{sha256}` (quirk-only)
* `UEFI\CRT_{sha256}&ARCH_{arch}`

...where `arch` is typically one of `IA32`, `X64`, `ARM` or `AA64`

Additionally, the last listed dbx SHA256 checksum is added as a quirk-only GUID so that the version
can be corrected even when fwupd is operating 100% offline.

* `UEFI\CSUM_{sha256}`

## Metadata

Microsoft actually removes checksums in some UEFI dbx updates, which is probably a result of OEM
pressure about SPI usage -- but local dbx updates are append-only.
This means that if you remove hashes then you can have a different number of dbx checksums on your
machine depending on whether you went `A→B→C→D` or `A→D`...

In these cases we look at the *last-entry* dbx checksum and compare to the set we know, either from
the quirk files, local metadata, or remote metadata from the LVFS.

The `org.linuxfoundation.dbx.*.firmware` components will match against a hash of the system PK.
The latest cabinet archive can also be installed into the `vendor-firmware`
remote found in `/usr/share/fwupd/remotes.d/vendor/firmware/` which allows the version-fixup to work
even when offline -- although using the LVFS source is recommended for most users.

The *last-entry checksum* can be found from the
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

This plugin has been available since fwupd version `1.5.0` with `org.uefi.dbx2`
being available in fwupd 1.9.27 (from the 1.9.x series) and 2.0.4 (from the 2.0.x series).
