Platform Health Assessment Table
================================

Introduction
------------

The PHAT is an ACPI table where a platform can expose health related telemetry
that may be useful for software running within the constraints of an OS.

These elements are typically going to encompass things that are likely otherwise
not enumerable during the OS runtime phase of operations, such as version of
pre-OS components.

The daemon includes some of the PHAT data in the report data sent to the LVFS
so that we can debug failures with the help of the IHV. This allows us to find
the root cause of the problem, and so we know what other OEMs may be affected.

See https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/05_ACPI_Software_Programming_Model/ACPI_Software_Programming_Model.html#platform-health-assessment-table
for more information.

External interface access
-------------------------
This plugin requires read access to `/sys/firmware/acpi/tables`.
