---
title: Plugin: CPU Microcode
---

## Introduction

This plugin reads the sysfs attributes associated with CPU microcode.
It displays a read-only value of the CPU microcode version loaded onto
the physical CPU at fwupd startup.

## GUID Generation

These devices add extra instance IDs from the CPUID values, e.g.

* `CPUID\PRO_0&FAM_06` (only-quirk)
* `CPUID\PRO_0&FAM_06&MOD_0E`
* `CPUID\PRO_0&FAM_06&MOD_0E&STP_3`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### CpuMitigationsRequired

Mitigations required for this specific CPU. Valid values are:

* `gds`

Since: 1.9.4

* `sinkclose`

Since: 2.0.2

### CpuSinkcloseMicrocodeVersion

Minimum version of microcode to mitigate the `sinkclose` vulnerability.

Since: 2.0.2

## External Interface Access

This plugin requires no extra access.

## Version Considerations

This plugin has been available since fwupd version `1.4.0`.
