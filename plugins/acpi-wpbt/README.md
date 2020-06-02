ACPI WPBT
=========

Introduction
------------

This plugin checks if the WPBT ACPI table has been exported. Although unused by
Linux, Microsoft Windows will execute the EXE file specified by the WPBT table.
The launching of the executable may be conditionalized on it being signed by a
specific cryptographic key or can depend on site or system policy.

WPBT should be disabled in the machine firmware setup, and it certainly should
not be enabled by default at the factory. Most commonly WPBT is enabled when
enabling the Computrace feature.

Computrace uses WPBT for legitimate hardware recovery purposes. Less commonly
an OEM may use it to install OEM-specific drivers and utilities.

The result will be stored in a security attribute for HSI.

If you wish to ignore the HSI failure (for example, because Computrace is
actually desired) then you can remove this runtime issue by setting
`BlacklistPlugins=acpi_wpbt` in `/etc/fwupd/daemon.conf`
