---
title: Plugin: TPM
---

## Introduction

This allows enumerating Trusted Platform Modules, also known as "TPM" devices,
although it does not allow the user to update the firmware on them.

The TPM Event Log records which events are registered for the PCR0 hash, which
may help in explaining why PCR0 values are differing for some firmware.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `org.trustedcomputinggroup.tpm2`

## GUID Generation

These devices use custom GUIDs:

* `TPM\VEN_$(manufacturer)&DEV_$(type)`
* `TPM\VEN_$(manufacturer)&MOD_$(vendor-string)`
* `TPM\VEN_$(manufacturer)&DEV_$(type)_VER_$(family)`,
* `TPM\VEN_$(manufacturer)&MOD_$(vendor-string)_VER_$(family)`

...where `family` is either `2.0` or `1.2`

Example GUIDs from a real system containing a TPM from Intel:

```text
  Guid:                 34801700-3a50-5b05-820c-fe14580e4c2d <- TPM\VEN_INTC&DEV_0000
  Guid:                 03f304f4-223e-54f4-b2c1-c3cf3b5817c6 <- TPM\VEN_INTC&DEV_0000&VER_2.0
```

## Update Behavior

The plugin detects if the TPM device is updatable (if the commands `FieldUpgradeStart` and
`FieldUpgradeData` are implemented) and provides stub functionality to read and write the firmware.
Nearly all TPMs in the wild have this behavior disabled, and are instead updated using
vendor-specific commands used from a UEFI UpdateCapsule and with an OEM-provided ESRT entry.

If a vendor wanted to use the plugin code provided here they would still need to tell us how to
set up the correct `keyHandle` for `FieldUpgradeStart` and how to handle authorization.

We do not know of any vendor that does TPM updates using the standardized API, and so the code
provided here is more of a *this is how it should be implemented* rather than with any expectation
it is actually going to just work.

## Vendor ID Security

The vendor ID is set from the TPM vendor, e.g. `TPM:STM`

## External Interface Access

This plugin uses the tpm2-tss library to access the TPM.  It requires access to `/sys/class/tpm`
and optionally requires read only access to `/sys/kernel/security/tpm0/binary_bios_measurements`.

## Version Considerations

This plugin has been available since fwupd version `1.3.6`.
