---
title: Plugin: UEFI Capsule
---

## Introduction

The Unified Extensible Firmware Interface (UEFI) is a specification that
defines the software interface between an OS and platform firmware.
With the UpdateCapsule boot service it can be used to update system firmware.

If you don't want or need this functionality you can use the
`-Dplugin_uefi_capsule=disabled` option.

When this plugin is enabled, the companion UEFI binary may also be built from the [fwupd-efi](https://github.com/fwupd/fwupd-efi) project if not already present on the filesystem.
This behavior can be overridden using the meson option `-Defi_binary=false`.

For this companion binary to work with secure boot, it will need to be signed by an authority trusted with shim and/or the host environment.

## Lenovo Specific Behavior

On Lenovo hardware only the boot label is set to `Linux-Firmware-Updater` rather
than "Linux Firmware Updater" (with spaces) due to long-fixed EFI boot manager
bugs. Many users will have these old BIOS versions installed and so we use the
`use-legacy-bootmgr-desc` quirk to use the safe name.

On some Lenovo hardware only one capsule is installable due to possible problems
with the UpdateCapsule coalesce operation. As soon as one UEFI device has been
scheduled for update the other UEFI devices found in the ESRT will be marked
as `updatable-hidden` rather than `updatable`. Rebooting will restore them so
they can be updated on next OS boot.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
EFI capsule file format.

See the [UEFI specification](https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf)
for details.

This plugin supports the following protocol ID:

* `org.uefi.capsule`

## Version Format

The ESRT table is the way the firmware tells fwupd about what devices are updatable. Unfortunately
the information it contains is minimal:

* GUID of the sub-component
* Enumerated result of the last update
* Firmware version as a **32 bit unsigned number**

By default, we show the ESRT devices as decimal or hexadecimal numbers as different vendors format
the number in different ways. When fwupd gets information about how to format the *raw* version
is converts the number into a more familiar form.

When the hardware GUID is static, a quirk may be appropriate, for example:

    [28108d08-5027-42c2-a5b8-92d6ede9b97b]
    VersionFormat = quad

As the GUID may be model specific, the daemon also reads the metadata and copies the version format
from that. This means that `fwupdmgr get-devices` may return the UEFI device as a number initially,
then once `fwupdmgr refresh` has completed it may start showing the exact same device as `A.B.C.D`,
aka `quad` format.

The two main formats used by vendors are `triplet`, `quad` and `dell-bios`.

    0xAABBCCDD -> 0xAA.0xBB.0xCCCC is `triplet`, used for Lenovo
    0xAABBCCDD -> 0xAA.0xBB.0xCC.0xDD is `quad`, used for HP
    0xAABBCCDD -> 0xBB.0xCC.0xDD is `dell-bios`, used for Dell

There are more details about firmware version formats and a full list of all the different allowed
values on the [LVFS](https://lvfs.readthedocs.io/en/latest/metainfo.html#version-format).

NOTE: Firmware can require either the `quad` or `triplet` string version format, but it may be more
portable to depend on the number -- which will also work if the metadata has not been refreshed yet.

## Update Behavior

### Capsule update on-disk

Described in  [UEFI specification](https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf)
§ 8.5.5 - Delivery of Capsules via file on Mass Storage device.

If the firmware supports this, it will be the preferred method of updating on
aarch64 platforms. You can explicitly disable it by by modifying
*DisableCapsuleUpdateOnDisk* in `/etc/fwupd/uefi_capsule.conf`.

The spec expects runtime *SetVariable* to be available in order to enable this
feature, we need to set `EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED`
in *OsIndications* variable to trigger processing of submitted capsule on next
reboot. However some firmware implementations (e.g U-Boot), can't set the
variable at runtime, but ignore the variable in next reboot and apply the
capsule anyway.

The directory \EFI\UpdateCapsule is checked for capsules only within the EFI
system partition on the device specified in the active boot option determine by
reference to *BootNext* variable or *BootOrder* variable processing.  Since
setting *BootNext*, for capsule update on-disk, is not yet implemented, the only
available option is place the \EFI\UpdateCapsule within the ESP partition
indicated by the current *BootOrder*.
Note that this will be always needed if your firmware doesn't support
*SetVariable* at runtime (even if *BootNext* functionality is added).

### Runtime capsule updates

The firmware is deployed when the OS is running, but it is only written when the
system has been restarted and the `fwupd*.efi` binary has been run. To achieve
this fwupd sets up the EFI `BootNext` variable, creating the new boot entry if
required.

## GUID Generation

These devices use the UEFI GUID as provided in the ESRT. Additionally, for the
system device the `main-system-firmware` GUID is also added.

For compatibility with Windows 10, the plugin also adds GUIDs of the form
`UEFI\RES_{$(esrt)}`.

## Vendor ID Security

The vendor ID is set from the BIOS vendor, for example `DMI:LENOVO` for all
devices that are not marked as supporting Firmware Management Protocol. For FMP
device no vendor ID is set.

## UEFI Unlock Support

On some Dell systems it is possible to turn on and off UEFI capsule
support from within the BIOS.  This functionality can also be adjusted
from within the OS by fwupd. This requires compiling with libsmbios support.

When fwupd has been compiled with this support you will be able to enable UEFI
support on the device by using the `unlock` command.

## Custom EFI System Partition (ESP)

Since version 1.1.0 fwupd will autodetect the ESP if it is mounted on
`/boot/efi`, `/boot`, or `/efi`, and UDisks is available on the system. In
other cases the mount point of the ESP needs to be manually specified using the
option *EspLocation* in `/etc/fwupd/daemon.conf`.

Setting an invalid directory will disable the fwupd plugin.

## External Interface Access

This plugin requires:

* read/write access to the EFI system partition.
* read access to `/sys/firmware/efi/esrt/`
* read access to `/sys/firmware/efi/fw_platform_size`
* read/write access to `/sys/firmware/efi/efivars`

## Version Considerations

This plugin has been available since fwupd version `0.8.0` but was renamed to the current name in
`1.5.5`.
