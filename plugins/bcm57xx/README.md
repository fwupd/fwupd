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

Update Behavior
---------------

The device usually presents in runtime mode, and the firmware is written to the
device without disconnecting the working kernel driver. Once complete the APE
is reset which may cause a brief link reconnection.

On flash failure the device is nonfunctional, but is recoverable using direct
BAR writes, which is typically much slower than updating the device using the
kernel driver and the ethtool API.

Vendor ID Security
------------------

The vendor ID is set from the PCI vendor, in this instance set to `PCI:0x14E4`

External interface access
-------------------------
This plugin requires the `SIOCETHTOOL` ioctl interface.
