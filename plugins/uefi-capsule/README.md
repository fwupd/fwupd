UEFI Capsule Support
====================

Introduction
------------

The Unified Extensible Firmware Interface (UEFI) is a specification that
defines the software interface between an OS and platform firmware.
With the UpdateCapsule boot service it can be used to update system firmware.

If you don't want or need this functionality you can use the
`-Dplugin_uefi_capsule=false` option.

Lenovo Specific Behavior
------------------------

On Lenovo hardware only the boot label is set to `Linux-Firmware-Updater` rather
than "Linux Firmware Updater" (with spaces) due to long-fixed EFI boot manager
bugs. Many users will have these old BIOS versions installed and so we use the
`use-legacy-bootmgr-desc` quirk to use the safe name.

On some Lenovo hardware only one capsule is installable due to possible problems
with the UpdateCapsule coalesce operation. As soon as one UEFI device has been
scheduled for update the other UEFI devices found in the ESRT will be marked
as `updatable-hidden` rather than `updatable`. Rebooting will restore them so
they can be updated on next OS boot.

UEFI SBAT Support
-----------------

If compiling with `-Dsupported_build=true` the packager must also specify the
SBAT metadata required for the secure boot revocation support. See the
specification for more information: https://github.com/rhboot/shim/blob/sbat/SBAT.md

Typically, this will be set as part of the package build script, e.g.

    -Defi_sbat_distro_id="fedora" \
    -Defi_sbat_distro_summary="The Fedora Project" \
    -Defi_sbat_distro_pkgname="%{name}" \
    -Defi_sbat_distro_version="%{version}" \
    -Defi_sbat_distro_url="https://src.fedoraproject.org/rpms/%{name}" \

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
EFI capsule file format.

See https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf
for details.

This plugin supports the following protocol ID:

 * org.uefi.capsule

Update Behavior
---------------

The firmware is deployed when the OS is running, but it is only written when the
system has been restarted and the `fwupd*.efi` binary has been run. To achieve
this fwupd sets up the EFI `BootNext` variable, creating the new boot entry if
required.

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
used by modifying *OverrideESPMountPoint* in `/etc/fwupd/uefi_capsule.conf`.

Setting an invalid directory will disable the fwupd plugin.

External interface access
-------------------------
This plugin requires:
* read/write access to the EFI system partition.
* read access to `/sys/firmware/efi/esrt/`
* read access to `/sys/firmware/efi/fw_platform_size`
* read/write access to `/sys/firmware/efi/efivars`
