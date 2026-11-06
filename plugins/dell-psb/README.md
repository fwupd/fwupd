---
title: Plugin: Dell PSB
---

## Introduction

The Dell PSB is the PCIe switch board of the PowerEdge XE9680, XE8640 and XE9640
servers. It is a PCI function `1000:00B2` that exposes a SCSI generic node, and
its firmware is flashed over SCSI WRITE BUFFER(10).

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format. The blob is written to the switch verbatim.

This plugin supports the following protocol ID:

* `com.dell.psb`

## GUID Generation

These devices use the standard PCI DeviceInstanceId values, e.g.

* `PCI\VEN_1000&DEV_00B2`
* `PCI\VEN_1000&DEV_00B2&SUBSYS_10282330`

## Vendor ID Security

Vendor IDs are set automatically by the PCI subsystem, using the `VEN` and `DEV`
instance keys.
