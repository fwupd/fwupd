---
title: Plugin: TPM
---

## Introduction

This allows enumerating Trusted Platform Modules, also known as "TPM" devices,
although it does not allow the user to update the firmware on them.

The TPM Event Log records which events are registered for the PCR0 hash, which
may help in explaining why PCR0 values are differing for some firmware.

The device exposed is not upgradable in any way and is just for debugging.
The created device will be a child device of the system TPM device, which may
or may not be upgradable.

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

## Vendor ID Security

The device is not upgradable and thus requires no vendor ID set.

## External Interface Access

This plugin uses the tpm2-tss library to access the TPM.  It requires access to `/sys/class/tpm`
and optionally requires read only access to `/sys/kernel/security/tpm0/binary_bios_measurements`.
