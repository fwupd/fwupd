CPU Microcode Support
=================

Introduction
------------

This plugin reads the sysfs attributes associated with CPU microcode.
It displays a read-only value of the CPU microcode version loaded onto
the physical CPU at fwupd startup.

GUID Generation
---------------

These devices add extra instance IDs from the CPUID values, e.g.

 * `CPUID\PRO_0&FAM_6`
 * `CPUID\PRO_0&FAM_6&MOD_E`
 * `CPUID\PRO_0&FAM_6&MOD_E&STP_3`
