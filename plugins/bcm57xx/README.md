BCM57xx Support
===============

Introduction
------------

This plugin updates BCM57xx wired network adaptors from Broadcom using a
reverse-engineered flashing protocol. It is designed to be used with the
clean-room reimplementation of the BCM5719 firmware found here:
https://github.com/meklort/bcm5719-fw

Protocol
--------
BCM57xx devices support a custom `com.broadcom.bcm57xx` protocol which is
implemented as ioctls like ethtool does.

GUID Generation
---------------

These devices use the standard PCI instance IDs, for example:

 * `PCI\VEN_14E4&DEV_1657`
 * `PCI\VEN_14E4&DEV_1657&SUBSYS_17AA222E`

Vendor ID Security
------------------

The vendor ID is set from the PCI vendor, in this instance set to `PCI:0x14E4`
