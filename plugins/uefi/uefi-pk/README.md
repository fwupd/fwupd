---
title: Plugin: UEFI PK
---

## Introduction

The platform key (PK) specifies the machine owner, typically the OEM
that created the laptop or desktop.

Several device manufacturers decide to ship the default "AMI Test PK"
platform key instead of a Device Manufacturer specific one. This will
cause an HSI-1 failure.

## GUID Generation

These devices use the GUID constructed of the uppercase SHA256 of the X.509 certificate. e.g.

* `UEFI\CRT_{sha256}`

Additionally, the subject vendor and name are used if provided.

* `UEFI\VENDOR_{vendor}&NAME_{name}`

These GUIDs can be used to fulfill "other device" requirements when installing `KEK` updates.

## External Interface Access

This plugin requires:

* read access to `/sys/firmware/efi/efivars`

## Version Considerations

This plugin has been available since fwupd version `1.5.5`.
