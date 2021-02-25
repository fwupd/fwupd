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

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, but it is
only activated when the device is rebooted.

Vendor ID Security
------------------

The vendor ID is set from the EMMC vendor, for example set to `EMMC:{$manfid}`

External interface access
-------------------------
This plugin requires ioctl `MMC_IOC_CMD` and `MMC_IOC_MULTI_CMD` access.
