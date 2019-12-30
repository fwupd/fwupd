UEFI Support
============

Introduction
------------

Some devices have firmware bugs which mean they do not include a valid ESRT
table in old firmware versions.

Create a 'fake' UEFI device with the lowest possible version so that it can be
updated to a version of firmware which does have an ESRT table.

GUID Generation
---------------

All the HwId GUIDs are used for the fake UEFI device, and so should be used in
the firmware metadata for releases that should recover the system.

Vendor ID Security
------------------

The vendor ID is set from the BIOS vendor, for example `DMI:LENOVO`
