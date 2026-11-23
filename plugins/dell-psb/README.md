---
title: Plugin: Dell PSB
---

## Introduction

The Dell PSB is the PCIe switch board of the PowerEdge XE9680, XE8640 and XE9640
servers. It is a PCI function `1000:00B2` that exposes a SCSI generic node, and
its firmware is flashed over SCSI WRITE BUFFER(10).

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format. The blob is written to the switch verbatim,
in 32 KiB chunks, using SCSI WRITE BUFFER(10) mode `0x02`.

The running version is read from the `@(#)PCI-<version> <date>` banner near the
start of the firmware image, which is exposed read-only through SCSI
READ BUFFER(10) with buffer ID `0x00`. For example:

```
@(#)PCI-4.160.3.0 09/27/2024
@(#)Dell Fryer U.2 SW1
```

The WRITE BUFFER(10) buffer offset is a 24-bit field, so images larger than
16 MiB are rejected before any chunk is written.

The switch only activates the new image on the next power cycle, so the device
is marked as needing a reboot before the update starts.

This plugin supports the following protocol ID:

* `com.dell.psb`

## GUID Generation

These devices use the standard PCI DeviceInstanceId values, e.g.

* `PCI\VEN_1000&DEV_00B2`
* `PCI\VEN_1000&DEV_00B2&SUBSYS_10282330`

## Vendor ID Security

Vendor IDs are set automatically by the PCI subsystem, using the `VEN` and `DEV`
instance keys.

## Device Matching

The plugin watches the `scsi_generic` udev subsystem. Which PCI function is the
PSB switch is decided by the quirk database, not by the plugin: the
`DellPsbDevice` key is looked up against the PCI parent's instance ID.

```ini
[PCI\VEN_1000&DEV_00B2]
DellPsbDevice = true
```

Adding a new switch therefore needs only a quirk entry, not a code change.
