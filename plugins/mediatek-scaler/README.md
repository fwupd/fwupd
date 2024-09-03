---
title: Plugin: Mediatek Display Controller
---

## Introduction

This plugin updates the firmware of DisplayPort device made by Mediatek.

These devices communicate over I²C, via the DisplayPort aux channel. Devices
are declared by kernel graphic driver, and queried with custom DDC/CI command
to ensure the target devie.

This plugin polls every drm dp aux device to find out the `i2c-dev` that is
being used for DDC/CI communication. Devices should respond to a vendor specific
command otherwise the display controller is ignored as unsupported.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is written to the partition of the
device flash that is not currently running.

This plugin supports the following protocol ID:

* `com.mediatek.scaler`

## GUID Generation

Devices use instance ID and the ingredients are vary according to fwupd version.

### fwupd 1.9.6

Devices use instance ID that is composed of `Subsystem ID`, `Subsystem Model`, and
the `Hardware Version`. The hardware version is read from the device.

* `DISPLAY\VID_1028&PID_0C88&HWVER_2.1.2.1` (metadata)
* `DISPLAY\VID_1028&PID_0C8A&HWVER_2.1.2.1` (metadata)

### fwupd 1.9.10

The DDC/CI commands used to test the target device have been restricted to run on
specific hardware only. Typically by adding a quirk file to match the system `VID`
and `PID` from the `PCI` bus.

* `DISPLAY\VID_1028&PID_0C88&HWVER_2.1.2.1` (metadata)
* `DISPLAY\VID_1028&PID_0C8A&HWVER_2.1.2.1` (metadata)
* `PCI\VID_1028&PID_0C88` (only-quirks)
* `PCI\VID_1028&PID_0C8A` (only-quirks)

### fwupd 2.0.0

The enumeration of `i2c` devices has been centralized to daemon libfwupdplugin and
declared as a `proxy` device for the `drm` subsystem device. The instance ID will
be composed of `VEN`, `DEV` and the `HWVER`.

The instance ID `VEN` is equivalent to the `mfg id` in device `EDID` while `DEV` is
the `product code`.

* `DRM\VEN_DEL&DEV_4340&HWVER_2.1.2.1` (quirks and metadata)
* `DRM\VEN_DEL&DEV_7430&HWVER_3.1.5.1` (quirks and metadata)

Example below for the information in EDID:

```shell
$ sudo apt install ddcutil
$ sudo ddcutil detect
Display 1
   I2C bus:  /dev/i2c-14
   DRM connector:           card1-DP-2
   EDID synopsis:
      Mfg id:               DEL - Dell Inc.
      Model:                OptiPlex AIO
      Product code:         17216  (0x4340)
      Serial number:
      Binary serial number: 1145581640 (0x44483048)
      Manufacture year:     2023,  Week: 30
   VCP version:         2.1
```

## Update Behavior

The firmware is deployed when the device is in normal runtime mode, and the
device will reset when the new firmware has been written. On some hardware the
DRM device may not enumerate if there is no monitor actually plugged in.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### Flags: `probe-vcp`

This flag is used if the VCP should be probed.

### Flags: `bank2-only`

Install firmware to bank 2 only.

Since: 2.0.0

## Vendor ID security

The vendor ID is set from the PCI vendor, for instance `PCI:0x1028` on Dell systems.

## External Interface Access

This plugin requires access to i2c buses associated with the specified
DisplayPort aux channel, for instance `/dev/i2c-5` and `/dev/drm_dp_aux3`. Note that
the device number changes in various situations.

## Version Considerations

This plugin has been available since fwupd version `1.9.6`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Crag Wang: @CragW
* Greg Lo: @GregLo007
