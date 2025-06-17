---
title: Plugin: UEFI KEK
---

## Introduction

The EFI Key Exchange Key (also known as `KEK`) contains multiple certificates used to update the
`db`, all signed by the `PK`.

This plugin allows updating the `KEK` with new certificates signed by the OEM vendor.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in `EFI_SIGNATURE_LIST`
format.
See <https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf> for details.

This plugin supports the following protocol ID:

* `org.uefi.dbx2`

## GUID Generation

These devices use the GUID constructed of the uppercase SHA256 of the X.509 certificate. e.g.

* `UEFI\CRT_{sha256}`

Additionally, the subject vendor and name are used if provided.

* `UEFI\VENDOR_{vendor}&NAME_{name}`

## Update Behavior

The firmware is deployed when the machine is in normal runtime mode, but it is only activated when
the system is restarted.

## Vendor ID Security

The vendor ID is set from the certificate vendor, e.g. `UEFI:Microsoft`.

## External Interface Access

This plugin requires:

* Read and write access to `/sys/firmware/efi/efivars`

## Version Considerations

This plugin has been available since fwupd version `2.0.8`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Richard Hughes: @hughsie
