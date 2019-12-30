Flashrom
========

Introduction
------------

This plugin uses `flashrom` to update the system firmware.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is typically the raw input for an
EEPROM programmer.

This plugin supports the following protocol ID:

 * org.flashrom

GUID Generation
---------------

These device uses hardware ID values which are derived from SMBIOS. They should
match the values provided by `fwupdtool hwids` or the `ComputerHardwareIds.exe`
Windows utility.

Vendor ID Security
------------------

The vendor ID is set from the BIOS vendor, for example `DMI:Google`
