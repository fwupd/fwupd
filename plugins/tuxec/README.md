Tuxec - Tuxedo Embedded Controller
========

Introduction
------------

This plugin uses `flashrom` to update the embedded controller firmware
in Tuxedo laptops.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is typically the raw input for an
EEPROM programmer.

This plugin supports the following protocol ID:

 * com.tuxedo.ec

GUID Generation
---------------

Internal device uses hardware ID values which are derived from SMBIOS.

 * HardwareID-3
 * HardwareID-4
 * HardwareID-5
 * HardwareID-6
 * HardwareID-10

They should match the values provided by `fwupdtool hwids` or the
`ComputerHardwareIds.exe` Windows utility.

Update Behavior
---------------

The firmware is deployed to the SPI chip when the machine is in normal runtime
mode, but it is only used when the device is rebooted.

External interface access
---
This plugin requires access to all interfaces that `libflashrom` has been compiled for.
This typically is `/sys/bus/spi` but there may be other interfaces as well.
