---
title: Plugin: Dell
---

## Introduction

This allows installing Dell capsules that are not part of the ESRT table.

## GUID Generation

These devices uses custom GUIDs for Dell-specific hardware.

* Thunderbolt devices: `TBT-0x00d4u$(system-id)`
* TPM devices `$(system-id)-$(mode)`, where `mode` is either `2.0` or `1.2`

In both cases the `system-id` is derived from the SMBIOS Product SKU property.

TPM GUIDs are also built using the TSS properties
`TPM2_PT_FAMILY_INDICATOR`, `TPM2_PT_MANUFACTURER`, and `TPM2_PT_VENDOR_STRING_*`
These are built hierarchically with more parts for each GUID:

* `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1`
* `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1$VENDOR_STRING_2`
* `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1$VENDOR_STRING_2$VENDOR_STRING_3`
* `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1$VENDOR_STRING_2$VENDOR_STRING_3$VENDOR_STRING_4`

If there are non-ASCII values in any vendor string or any vendor is missing that octet will be skipped.

Example resultant GUIDs from a real system containing a TPM from Nuvoton:

```text
  Guid:                 7d65b10b-bb24-552d-ade5-590b3b278188 <- DELL-TPM-2.0-NTC-NPCT
  Guid:                 6f5ddd3a-8339-5b2a-b9a6-cf3b92f6c86d <- DELL-TPM-2.0-NTC-NPCT75x
  Guid:                 fe462d4a-e48f-5069-9172-47330fc5e838 <- DELL-TPM-2.0-NTC-NPCT75xrls
```

## Devices powered by the Dell Plugin

The Dell plugin creates device nodes for PC's with upgradable TPMs.

These device nodes can be flashed using UEFI capsule but don't
use the ESRT table to communicate device status or version information.

This is intentional behavior because more complicated decisions need to be made
on the OS side to determine if the devices should be offered to flash.

## External Interface Access

This plugin requires read/write access to `/dev/wmi/dell-smbios` and `/sys/bus/platform/devices/dcdbas`.

## Version Considerations

This plugin has been available since fwupd version `0.8.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Mario Limonciello: @superm1
