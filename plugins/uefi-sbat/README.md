---
title: Plugin: UEFI SBAT
---

## Introduction

SBAT is a generation number based revocation mechanism that is used as a successsor to dbx checksum
blocklisting.

See [https://github.com/rhboot/shim/blob/main/SBAT.md](the specification) for more details.

## Firmware Format

The daemon will decompress the cabinet archive and extract a signed PE/COFF firmware blob with a
`.sbata` section containing the revocation CSV data.

This plugin supports the following protocol ID:

* `org.uefi.sbat`

## GUID Generation

These devices use the distribution-specific DeviceInstanceId values, e.g.

* `UEFI\OS_fedora` (only quirk)
* `UEFI\OS_fedora&VAR_SbatLevelRT`

## Update Behavior

The device version is calculated using the same heuristics as for the revocation PE file, which is
complicated slightly by there being potentially multiple components (each with a version) in the
payload, for instance, `shim`, `grub` and `sd-boot`.

These revocation components may or may not exist in the ESP, and may not exist in the existing
platform UEFI `SbatLevelRT` variable, and so it is impossible to create a set of child-devices
for each item like we might do for a USB device for example.

Additionally, assigning *a single version* is complicated further because per-vendor overrides may
be present in the SBAT entries in either the `SbatLevelRT` key or the signed revocation file.

To overcome these limitations (and to force a round peg in a square hole) we sum up each generation
number contained in the SBAT sections, **including** the per-vendor value where provided.

The semver *major* value is the SBAT revision, the *minor* value is the sum of all the upstream
component generation numbers, and the *micro* value is the sum of all the downstream component
generation numbers.

For example:

A single component:

    sbat,1
    -> "version 1.0.0"

Multiple components:

    sbat,1
    grub,4
    -> "version 1.4.0"

Multiple components, with a distro specific override:

    sbat,1
    grub,4
    sd-boot,2
    grub.fedora,2
    grub.ubuntu,2
    -> "version 1.7.4"

This ensures that we can have a single version number representing the device current state, and
also a single version number that represents the most up to date **per-distro** revocation data.

For this reason different distributions should have a different AppStream namespace for the
revocation data, for instance `org.fedoraproject.sbat.firmware` -- which makes sense as each vendor
will be signing the revocation binary with its own key anyway.

## Deployment Concerns

Before deploying the contents of the `.sbata` COFF section to the `SbatLevelRT` UEFI variable we
should check that we wouldn't be *soft bricking* the users PC by deploying a SBAT policy that cannot
allow the current shim and second stage bootloader to run on next reboot.

Although the `SbatLevelRT` UEFI variable is deleted when SecureBoot is turned off, this is not
something we should be asking end users to do.

By locating each `BootXXXX` file path on the ESP we can load the `.sbat` section and only deploy
the revocation if the generation number of the EFI binary is greater than or equal to the specific
generation number specified in the revocation.

For instance, if there is a boot entry `Boot0001 -> shim.efi` we would:

* Read `ESP/shim.efi` COFF section `.sbat`:

    sbat,1
    shim,4
    shim.rh,3
    shim.fedora,3

* Read `ESP/grubx64.efi` (this filename is hardcoded into `ESP/shim.efi`) COFF section `.sbat`

    sbat,1
    grub,3
    grub.rh,2

* Verify that the `.sbata` section in the `revocations.efi` payload is not newer that each of the
   file-defined SBAT values:

    sbat,1
    shim,2
    grub,3
    grub.debian,4

## Vendor ID Security

The vendor ID is set from the *operating system* vendor as discovered from `os-release`, in this
instance set to `OS:fedora`

## External Interface Access

This plugin requires read access to `/sys/firmware/efi/efivars` and read/write access to the ESP.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Testing

    $ sudo fwupdtool --plugins uefi-sbat  -vv install-blob revocations.efi.signed
    $ sudo fwupdtool --plugins uefi-sbat  -vv reboot-cleanup
    $ sudo fwupdtool firmware-build ../plugins/uefi-sbat/revocation.builder.xml revocation.efi
    Decompressing…           [************                           ]
    SBAT level is too old on /boot/efi/EFI/fedora/grubx64.efi: ESP file /boot/efi/EFI/fedora/shimx64.efi has SBAT entry sbat v1, but revocation has v2

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Peter Jones: @vathpela
