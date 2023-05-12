---
title: Plugin: Intel ME
---

## Introduction

This plugin is used to talk to the Intel ME device, typically CSME.

It allows us to get the Platform Key as used for BootGuard and to get the
version number for the Intel AMT.

If AMT is enabled and provisioned and the AMT version is between 6.0 and 11.2,
and you have not upgraded your firmware, you are vulnerable to CVE-2017-5689 and
you should disable AMT in your system firmware.

This code is inspired by 'AMT status checker for Linux' by Matthew Garrett
which can be found here: <https://github.com/mjg59/mei-amt-check>

That tool in turn is heavily based on mei-amt-version from samples/mei in the
Linux source tree and copyright Intel Corporation.

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

This plugin has been available since fwupd version `1.8.7`.
