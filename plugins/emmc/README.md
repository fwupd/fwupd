eMMC Support
=================

Introduction
------------

This plugin reads the sysfs attributes corresponding to eMMC devices.
It uses the kernel MMC API for flashing devices.

Protocol
--------
eMMC devices support the `org.jedec.mmc` protocol.

GUID Generation
---------------

These devices use the following instance values:

 * `EMMC\%NAME%`
 * `EMMC\%MANFID%&%OEMID%`
 * `EMMC\%MANFID%&%OEMID%&%NAME%`

Vendor ID Security
------------------

The vendor ID is set from the EMMC vendor, for example set to `EMMC:{$manfid}`
