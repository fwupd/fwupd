UEFI Support
============

Introduction
------------

The Unified Extensible Firmware Interface (UEFI) is a specification that
defines the software interface between an OS and platform firmware.
With the UpdateCapsule boot service it can be used to update system firmware.

If you don't want or need this functionality you can use the
`-Dplugin_uefi=false` option.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
EFI capsule file format.

See https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf
for details.

This plugin supports the following protocol ID:

 * org.uefi.capsule

GUID Generation
---------------

These devices use the UEFI GUID as provided in the ESRT. Additionally, for the
system device the `main-system-firmware` GUID is also added.

For compatibility with Windows 10, the plugin also adds GUIDs of the form
`UEFI\RES_{$(esrt)}`.

Vendor ID Security
------------------

The vendor ID is set from the BIOS vendor, for example `DMI:LENOVO` for all
devices that are not marked as supporting Firmware Management Protocol. For FMP
device no vendor ID is set.

UEFI Unlock Support
-------------------

On some Dell systems it is possible to turn on and off UEFI capsule
support from within the BIOS.  This functionality can also be adjusted
from within the OS by fwupd. This requires compiling with libsmbios support.

When fwupd has been compiled with this support you will be able to enable UEFI
support on the device by using the `unlock` command.

Custom EFI System Partition
---------------------------

Since version 1.1.0 fwupd will autodetect the ESP when it is mounted on
`/boot/efi`, `/boot`, or `/efi`. A custom EFI system partition location can be
used by modifying *OverrideESPMountPoint* in `/etc/fwupd/uefi.conf`.

Setting an invalid directory will disable the fwupd plugin.
