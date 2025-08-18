---
title: Plugin: B&R DisplayPort receiver
---

## Introduction

This plugin updates the firmware of DisplayPort receivers by B&R Industrial
Automation GmbH over the DisplayPort aux channel.

## Firmware Format

The daemon will decompress the cabinet archive and extract a single firmware
blob in a binary file format. The firmware file includes a proprietary XML
header with general metadata for update tools. The plugin makes use of this data
as needed. The XML header is separated by a null byte from the actual firmware
payload that needs to be written to the device in chunks.

This plugin supports the following protocol ID:

- `com.br-automation.dpaux`

## GUID Generation

These devices build their GUIDs by adding the `OUI` from the DpAux DPCD with
additional B&R-specific device, variant and revision metadata, e.g.

- `DPAUX\OUI_00006065&DEV_00002F1A`
- `DPAUX\OUI_00006065&DEV_00002F1A&VARIANT_00000000`
- `DPAUX\OUI_00006065&DEV_00002F1A&VARIANT_00000000&HW_REV_A0`

## Update Behavior

During normal runtime, the firmware is written to the low/high (A/B) section
that is currently not in use. The boot counter needs to be adjusted
appropriately to ensure that this newly written firmware is the one that is
preferred when the device boots. To activate the new firmware a reset of the
device is necessary, this will cause a brief screen flicker. Aside from the
brief restart at the end of the update, the device stays usable during most of
the update procedure.

## Vendor ID Security

The vendor ID is set from the OUI, i.e. `OUI:006065*`.

## Quirk Use

This plugin currently does not use any plugin-specific quirks.

## External Interface Access

This plugin requires read/write access to `/dev/drm_dp_aux*`.

## Version Considerations

This plugin has been available since fwupd version `2.0.7`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people
should be consulted before making major or functional changes:

- Thomas Mühlbacher: @tmuehlbacher-bnr
