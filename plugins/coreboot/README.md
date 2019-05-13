coreboot
========

WIP
------------
Depends on https://github.com/9elements/linux/tree/google_firmware_fmap2
Depends on https://review.coreboot.org/c/coreboot/+/35377

Introduction
------------

This plugin uses the `flashrom` plugin to update the system firmware.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is typically a VBOOT partition
containing at least one CBFS and a version string.

This plugin supports the following protocol ID:

 * org.coreboot.fmap

GUID Generation
---------------

These device uses hardware ID values which are derived from SMBIOS. They should
match the values provided by `fwupdtool hwids` or the `ComputerHardwareIds.exe`
Windows utility.
