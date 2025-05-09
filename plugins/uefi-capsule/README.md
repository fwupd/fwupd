---
title: Plugin: UEFI Capsule
---

## Introduction

The Unified Extensible Firmware Interface (UEFI) is a specification that
defines the software interface between an OS and platform firmware.
With the UpdateCapsule boot service it can be used to update system firmware.

When this plugin is enabled, the companion UEFI binary may also be built from the [fwupd-efi](https://github.com/fwupd/fwupd-efi) project if not already present on the filesystem by using the meson option `-Defi_binary=true`.

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

The main formats used by vendors are `triplet`, `quad`, `dell-bios` and `dell-bios-msb`.

    0xAABBCCDD -> 0xAA.0xBB.0xCCCC is `triplet`, used for Lenovo
    0xAABBCCDD -> 0xAA.0xBB.0xCC.0xDD is `quad`, used for HP
    0xAABBCCDD -> 0xBB.0xCC.0xDD is `dell-bios`, used for Dell
    0xAABBCCDD -> 0xAA.0xBB.0xCC is `dell-bios-msb`, used for Dell since CY24

There are more details about firmware version formats and a full list of all the different allowed
values on the [LVFS](https://lvfs.readthedocs.io/en/latest/metainfo.html#version-format).

NOTE: Firmware can require either the `quad` or `triplet` string version format, but it may be more
portable to depend on the number -- which will also work if the metadata has not been refreshed yet.

## Update Behavior

### Capsule update on-disk

Described in  [UEFI specification](https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf)
§ 8.5.5 - Delivery of Capsules via file on Mass Storage device.

If the firmware supports this, it will be the preferred method of updating when supported.
You can explicitly disable it by by modifying
*DisableCapsuleUpdateOnDisk* in the `uefi_capsule` section of `/etc/fwupd/fwupd.conf`.

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

These devices use the UEFI GUID as provided in the ESRT.

For compatibility with Windows 10, the plugin also adds GUIDs of the form
`UEFI\RES_{$(esrt)}`.

## Vendor ID Security

The vendor ID is set from the BIOS vendor, for example `DMI:LENOVO` for all
devices that are not marked as supporting Firmware Management Protocol. For FMP
device no vendor ID is set.

## Custom EFI System Partition (ESP)

Since version 1.1.0 fwupd will autodetect the ESP if it is mounted on
`/boot/efi`, `/boot`, or `/efi`, and UDisks is available on the system. In
other cases the mount point of the ESP needs to be manually specified using the
option *EspLocation* in `/etc/fwupd/fwupd.conf`.

Setting an invalid directory will disable the fwupd plugin.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=cod-indexed-filename`

Use a Capsule-on-Disk filename of `CapsuleUpdateFileXXXX.bin` rather than including the ESRT GUID.
This alternative format may be needed for some early InsydeH2O firmwares.

### `Flags=no-ux-capsule`

Do not use the additional UX capsule.

### `Flags=use-shim-unique`

Use a unique shim filename to work around a common BIOS bug.

### `Flags=use-legacy-bootmgr-desc`

Use the legacy boot manager description to work around a Lenovo BIOS bug.

### `Flags=supports-boot-order-lock`

The BIOS might have Boot Order Lock enabled which can cause failures when not using grub
chainloading or capsule-on-disk.

### `Flags=use-shim-for-sb`

Use shim to load fwupdx64.efi when SecureBoot is turned on.

### `Flags=no-rt-set-variable`

Do not use RT->SetVariable.

### `Flags=no-capsule-header-fixup`

Do not prepend a plausible missing capsule header.

### `Flags=enable-debugging`

Enable debugging the EFI binary.

### `Flags=modify-bootorder`

Modify `BootOrder` as well as `BootNext` to work around BIOS bugs.

### `Flags=cod-dell-recovery`

Use Dell customized file location for the capsule on disk.

### `Flags=no-esp-backup`

The UEFI ESRT entry does not need extra ESP space to write a backup ROM.
If this flag is set then the `RequireESPFreeSpace` config file option is ignored.

Since: 2.0.7

## External Interface Access

This plugin requires:

* read/write access to the EFI system partition.
* read access to `/sys/firmware/efi/esrt/`
* read access to `/sys/firmware/efi/fw_platform_size`
* read/write access to `/sys/firmware/efi/efivars`

## Version Considerations

This plugin has been available since fwupd version `0.8.0` but was renamed to the current name in
`1.5.5`.
