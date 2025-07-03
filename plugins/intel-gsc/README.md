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

```xml
<firmware gtype="FuIgscCodeFirmware">
  <id>BMG_</id>
  <version>0021.1111</version>
  <hw_sku>0x2</hw_sku>
  <arb_svn>0x2</arb_svn>
  <firmware>
    <id>INFO</id>
  </firmware>
  <firmware>
    <id>IMGI</id>
  </firmware>
  <firmware>
    <id>FWIM</id>
  </firmware>
</firmware>
```

### FWDATA

* This is a superset of `FuIfwiFptFirmware`
* The `INFO` partition is a `FuIfwiCpdFirmware` is a `FuStructIgscFwdataVersion`.
  This provides `oem_version`, `major_version` and `data_arb_svn`.
* The `SDTA` (also called a "GDTA") data partition is a `FuIfwiCpdFirmware`

```xml
<firmware gtype="FuIgscAuxFirmware">
  <oem_version>0x39</oem_version>
  <major_version>0xcb</major_version>
  <data_arb_svn>0x1</data_arb_svn>
  <device_infos>
    <match>
      <vendor_id>0x8086</vendor_id>
      <device_id>0xe20b</device_id>
      <subsys_vendor_id>0x8086</subsys_vendor_id>
      <subsys_device_id>0x1100</subsys_device_id>
    </match>
  </device_infos>
  <firmware>
    <id>INFO</id>
  </firmware>
  <firmware>
    <id>CKSM</id>
  </firmware>
  <firmware>
    <id>GDTA</id>
  </firmware>
</firmware>
```

* The `SDTA` is parsed as:

```xml
<firmware gtype="FuIfwiCpdFirmware">
  <header_version>0x2</header_version>
  <entry_version>0x1</entry_version>
  <firmware>
    <id>GDTA.man</id>
    <version_raw>0xcb</version_raw>
    <firmware>
      <idx>0x23</idx>
    </firmware>
    <firmware>
      <idx>0x16</idx>
    </firmware>
    <firmware>
      <idx>0x1d</idx>
    </firmware>
    <firmware>
      <idx>0x25</idx>
    </firmware>
  </firmware>
  <firmware>
    <id>GDTA</id>
  </firmware>
  <firmware>
    <id>GDTA.met</id>
  </firmware>
</firmware>
```

* The `GDTA.man` partition of the CPD contains the manifest extensions.

### OPROMCODE

* This must be only the *first* oprom image in the payload.
* The oprom expansion header is formatted as a `FuIfwiCpdFirmware` image
* The `OROM.man` section has two unknown (and unparsed) extension types

```xml
<firmware gtype="FuIgscOpromFirmware">
  <idx>0xf1</idx>
  <size>0x800</size>
  <firmware gtype="FuIfwiCpdFirmware">
    <id>cpd</id>
    <idx>0x4d4f524f</idx>
    <header_version>0x2</header_version>
    <entry_version>0x1</entry_version>
    <firmware>
      <id>OROM.man</id>
      <version_raw>0x4200017</version_raw>
      <firmware>
        <idx>0x23</idx>
      </firmware>
      <firmware>
        <idx>0x16</idx>
      </firmware>
    </firmware>
    <firmware>
      <id>CODE</id>
    </firmware>
    <firmware>
      <id>CODE.met</id>
    </firmware>
  </firmware>
</firmware>
```

### OPROMDATA

* This must be only the *first* oprom image in the payload.

```xml
<firmware gtype="FuIgscOpromFirmware">
  <idx>0xf0</idx>
  <size>0x800</size>
  <device_infos>
    <match>
      <vendor_id>0x8086</vendor_id>
      <device_id>0xe20b</device_id>
      <subsys_vendor_id>0x8086</subsys_vendor_id>
      <subsys_device_id>0x1100</subsys_device_id>
    </match>
  </device_infos>
  <firmware gtype="FuIfwiCpdFirmware">
    <id>cpd</id>
    <idx>0x4d4f524f</idx>
    <header_version>0x2</header_version>
    <entry_version>0x1</entry_version>
    <firmware>
      <id>OROM.man</id>
      <version_raw>0x4200017</version_raw>
      <firmware>
        <idx>0x23</idx>
      </firmware>
      <firmware>
        <idx>0x16</idx>
      </firmware>
      <firmware>
        <idx>0x25</idx>
      </firmware>
    </firmware>
    <firmware>
      <id>VBT</id>
      <idx>0x1</idx>
    </firmware>
    <firmware>
      <id>VBT.met</id>
      <idx>0x2</idx>
    </firmware>
  </firmware>
</firmware>
```

This plugin supports the following protocol ID, used by all devices and sub-devices:

* `com.intel.gsc`

## GUID Generation

These devices use the standard PCI DeviceInstanceId values, e.g.

* `PCI\VID_8086&DEV_4905`

They also define custom per-part PCI IDs such as:

* `PCI\VID_8086&DEV_4905&PART_FWCODE`
* `PCI\VID_8086&DEV_4905&PART_FWDATA`
* `PCI\VID_8086&DEV_4905&PART_OPROMCODE`
* `PCI\VID_8086&DEV_4905&PART_OPROMDATA`

When the device needs recovery, the instance IDs will instead be:

* `PCI\VEN_8086&DEV_4905&PART_FWCODE_RECOVERY`
* `PCI\VID_8086&DEV_4905&PART_FWDATA_RECOVERY`
* `PCI\VID_8086&DEV_4905&PART_OPROMCODE_RECOVERY`
* `PCI\VID_8086&DEV_4905&PART_OPROMDATA_RECOVERY`

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

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Vitaly Lubart: @vlubart
