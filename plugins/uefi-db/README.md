---
title: Plugin: UEFI db
---

## Introduction

The EFI Signature Database (also known as `db`) contains multiple certificates, hashes and
signatures, all signed by the KEK.

It is used to allow signed binaries to be run with SecureBoot enabled. This plugin allows updating
the `db` with new certificates issued by Microsoft or the OEM vendor.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in `EFI_SIGNATURE_LIST`
format.
See <https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf> for details.

This plugin supports the following protocol ID:

* `org.uefi.dbx2`

## GUID Generation

These devices use the GUID constructed of the uppercase SHA256 of the X.509 certificate. e.g.

* `UEFI\CRT_{sha256}`

If the device has been vendor-quirked with `use-db-default-ids` then the keys from `dbDefault`
are also added as a suffix:

* `UEFI\CRT_{sha256}&CRTD_{sha256}`

Additionally, the subject vendor and name are used if provided.

* `UEFI\VENDOR_{vendor}&NAME_{name}`

## Update Behavior

The firmware is deployed when the machine is in normal runtime mode, but it is only activated when
the system is restarted.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=use-db-default-ids`

Use the contents of `dbDefault` to build the instance ID.

For instance, for HP machines we want to only offer the 2023 UEFI db update to machines with
firmware new enough to not be bricked. We can identify these by looking at `dbDefault` and matching
the 2011 CA in `db` and the 2023 CA in `dbDefault`. e.g.
`UEFI\CRT_E30CF09DABEAB32A6E3B07A7135245DE05FFB658&CRTD_7CD7437C555F89E7C2B50E21937E420C4E583E80`

Since: 2.1.5

## Vendor ID Security

The vendor ID is set from the certificate vendor, e.g. `UEFI:Microsoft`.

## External Interface Access

This plugin requires:

* Read and write access to `/sys/firmware/efi/efivars`

## Version Considerations

This plugin has been available since fwupd version `2.0.8`.
