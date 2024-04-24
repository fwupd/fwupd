---
title: Plugin: Algoltek AUX
---

## Introduction

This plugin supports the firmware upgrade of DisplayPort over USB-C to HDMI converter provided by
Algoltek, Inc.
These DisplayPort over USB-C to HDMI converters can be updated through multiple interfaces,
but this plugin is only designed for the  DisplayPort AUX interface.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in a packed binary
file format.

This plugin supports the following protocol ID:

* `tw.com.algoltek.aux`

## GUID Generation

These devices use a custom instance ID consisting of `VEN` and `DEV` values, e.g.

* `MST\VEN_25A4&DEV_AG9421`

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the device will reset when
the new firmware has been programmed.

## External Interface Access

This plugin requires read/write access to `/dev/drm_dp_aux*`.

## Version Considerations

This plugin has been available since fwupd version `1.9.20`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

Jane Wu: @YiJaneWu
