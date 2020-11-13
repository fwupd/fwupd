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

 * `CPUID\PRO_0&FAM_06`
 * `CPUID\PRO_0&FAM_06&MOD_0E`
 * `CPUID\PRO_0&FAM_06&MOD_0E&STP_3`

External interface access
-------------------------
This plugin requires no extra access.
