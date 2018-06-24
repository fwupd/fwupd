UEFI Support
============

Introduction
------------

The Unified Extensible Firmware Interface (UEFI) is a specification that
defines the software interface between an OS and platform firmware.
With the UpdateCapsule boot service it can be used to update system firmware.

If you don't want or need this functionality you can use the
`--disable-uefi` option.

UEFI Unlock Support
-------------------

On some Dell systems it is possible to turn on and off UEFI capsule
support from within the BIOS.  This functionality can also be adjusted
from within the OS by fwupd. This requires compiling with libsmbios support.

When fwupd has been compiled with this support you will be able to enable UEFI
support on the device by using the `unlock` command.

Custom EFI System Partition
---------------------------

fwupd 1.0.6 and later can use a custom EFI system partition location by allowing
users to modify `/etc/fwupd/uefi.conf`.

An option titled *OverrideESPMountPoint* is available that can be
uncommented and set to any valid directory on the system.

Setting an invalid directory will disable the fwupd plugin.
