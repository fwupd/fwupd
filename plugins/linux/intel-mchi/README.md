---
title: Plugin: Intel MCHI
---

## Introduction

This plugin is used to talk to the Intel ME device using the Management Controller Host Interface
"MCHI" interface, also sometimes called "MCA".

It allows us to get the Platform Key as used for BootGuard.

## GUID Generation

These devices use the existing GUIDs provided by the ME host interfaces.

## Metadata

There have been several BootGuard key leaks that can be detected using the ME device.
The metadata needed to match the KM checksum is found in the metadata, typically obtained from
the LVFS project.
This sets the `leaked-km` private flag which then causes the HSI `org.fwupd.hsi.Mei.KeyManifest`
attribute to fail, and also adds a device inhibit which shows in the command line and GUI tools.

The `org.linuxfoundation.bootguard.config` component is currently used to match against both the
MCA and MKHI ME devices. The latest cabinet archive can also be installed into the `vendor-firmware`
remote found in `/usr/share/fwupd/remotes.d/vendor/firmware/` which allows the detection to work
even when offline -- although using the LVFS source is recommended for most users.

New *OEM Public Key Hash* values found from `MEInfo` or calculated manually should be added to the
checksums page on the LVFS.

## Vendor ID Security

The devices are not upgradable and thus require no vendor ID set.

## External Interface Access

This plugin requires `ioctl(IOCTL_MEI_CONNECT_CLIENT)` to `/dev/mei0`.

## Version Considerations

This plugin has been available since fwupd version `2.0.9`, although the functionality was
previously introduced in the `intel-me` plugin released with fwupd version `1.8.7`.
