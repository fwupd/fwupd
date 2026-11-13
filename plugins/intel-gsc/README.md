---
title: Plugin: Intel GSC — Graphics System Controller
---

## Introduction

This plugin is used to update the Intel graphics system controller via the Intel Management Engine.

## Firmware Format

### FWCODE

* This is a superset of `FuIfwiFptFirmware`
* The `INFO` partition is a `FuStructIgscFwuImageMetadataV1`, which gives the `id` and `version`,
  and the `FuStructIgscFwuFwImageData` is also part of that. This then gives the `arb_svn`.
* The `IMGI` partition is a `FuStructIgscFwuGwsImageInfo`, which gives us `hw_sku`

```json
{
  "gtype": "FuIgscCodeFirmware",
  "id": "BMG_",
  "version": "0021.1111",
  "hw_sku": "0x2",
  "arb_svn": "0x2",
  "firmware": [
    {
      "id": "INFO"
    },
    {
      "id": "IMGI"
    },
    {
      "id": "FWIM"
    }
  ]
}
```

### FWDATA

* This is a superset of `FuIfwiFptFirmware`
* The `INFO` partition is a `FuIfwiCpdFirmware` is a `FuStructIgscFwdataVersion`.
  This provides `oem_version`, `major_version` and `data_arb_svn`.
* The `SDTA` (also called a "GDTA") data partition is a `FuIfwiCpdFirmware`

```json
{
  "gtype": "FuIgscAuxFirmware",
  "oem_version": "0x39",
  "major_version": "0xcb",
  "data_arb_svn": "0x1",
  "device_infos": {
    "match": ""
  },
  "firmware": [
    {
      "id": "INFO"
    },
    {
      "id": "CKSM"
    },
    {
      "id": "GDTA"
    }
  ]
}
```

* The `SDTA` is parsed as:

```json
{
  "gtype": "FuIfwiCpdFirmware",
  "header_version": "0x2",
  "entry_version": "0x1",
  "firmware": [
    {
      "id": "GDTA.man",
      "version_raw": "0xcb",
      "firmware": [
        {
          "idx": "0x23"
        },
        {
          "idx": "0x16"
        },
        {
          "idx": "0x1d"
        },
        {
          "idx": "0x25"
        }
      ]
    },
    {
      "id": "GDTA"
    },
    {
      "id": "GDTA.met"
    }
  ]
}
```

* The `GDTA.man` partition of the CPD contains the manifest extensions.

### OPROMCODE

* The oprom expansion header is formatted as a `FuIfwiCpdFirmware` image
* The `OROM.man` section has two unknown (and unparsed) extension types

```json
{
  "gtype": "FuIgscOpromFirmware",
  "idx": "0xf1",
  "size": "0x800",
  "firmware": [
    {
      "gtype": "FuIfwiCpdFirmware",
      "id": "cpd",
      "idx": "0x4d4f524f",
      "header_version": "0x2",
      "entry_version": "0x1",
      "firmware": [
        {
          "id": "OROM.man",
          "version_raw": "0x4200017",
          "firmware": [
            {
              "idx": "0x23"
            },
            {
              "idx": "0x16"
            }
          ]
        },
        {
          "id": "CODE"
        },
        {
          "id": "CODE.met"
        }
      ]
    }
  ]
}
```

### OPROMDATA

```json
{
  "gtype": "FuIgscOpromFirmware",
  "idx": "0xf0",
  "size": "0x800",
  "device_infos": {
    "match": ""
  },
  "firmware": [
    {
      "gtype": "FuIfwiCpdFirmware",
      "id": "cpd",
      "idx": "0x4d4f524f",
      "header_version": "0x2",
      "entry_version": "0x1",
      "firmware": [
        {
          "id": "OROM.man",
          "version_raw": "0x4200017",
          "firmware": [
            {
              "idx": "0x23"
            },
            {
              "idx": "0x16"
            },
            {
              "idx": "0x25"
            }
          ]
        },
        {
          "id": "VBT",
          "idx": "0x1"
        },
        {
          "id": "VBT.met",
          "idx": "0x2"
        }
      ]
    }
  ]
}
```

This plugin supports the following protocol ID, used by all devices and sub-devices:

* `com.intel.gsc`

## GUID Generation

These devices use the standard PCI DeviceInstanceId values, e.g.

* `PCI\VEN_8086&DEV_E20B`

They also define custom per-part PCI IDs such as:

* `PCI\VEN_8086&DEV_E20B&PART_FWCODE`
* `PCI\VEN_8086&DEV_E20B&PART_FWDATA`
* `PCI\VEN_8086&DEV_E20B&PART_OPROMCODE`
* `PCI\VEN_8086&DEV_E20B&PART_OPROMDATA`

When the device needs recovery, the instance IDs will instead be:

* `PCI\VEN_8086&DEV_E20B&PART_FWCODE_RECOVERY`
* `PCI\VEN_8086&DEV_E20B&PART_FWDATA_RECOVERY`
* `PCI\VEN_8086&DEV_E20B&PART_OPROMCODE_RECOVERY`
* `PCI\VEN_8086&DEV_E20B&PART_OPROMDATA_RECOVERY`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=has-aux`

Has an AUX child device.

### `Flags=has-oprom`

Has an option ROM child device.

## Vendor ID Security

The vendor ID is set from the PCI vendor, in this instance set to `MEI:0x8086`

## External Interface Access

This plugin requires read/write access to `/dev/mei*`.

## Version Considerations

This plugin has been available since fwupd version `1.8.7`.
