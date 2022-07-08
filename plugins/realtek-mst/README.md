# Realtek MST

## Introduction

This plugin updates the firmware of DisplayPort MST hub devices made by Realtek,
such as the RTD2141b and RTD2142.

These devices communicate over I²C, via the DisplayPort aux channel. Devices
are declared by system firmware, and quirks specify the aux channel to which
the device is connected for a given system.

System firmware must specify the device's presence because while they can be
identified partially through the presence of Realtek's OUI in the Branch
Device OUI fields of DPCD (DisplayPort Configuration Data), they do not have
unique Device Identification strings.

This plugin was neither written, verified, supported or endorsed by Realtek
Semiconductor Corp.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is written to the partition of the
device flash that is not currently running.

This plugin supports the following protocol ID:

* com.realtek.rtd2142

## GUID Generation

Devices use an extra instance ID derived from SMBIOS, e.g.

* `I2C\NAME_10EC2142:00&FAMILY_Google_Hatch`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### RealtekMstDpAuxName

Specifies the name of the drm_dp_aux_dev channel over which the device should be reached.

Since: 1.6.2

## Vendor ID security

The vendor ID is specified by system firmware (such as ACPI tables).

## External Interface Access

This plugin requires access to i2c buses associated with the specified
DisplayPort aux channel, usually `/dev/i2c-5` or similar.
