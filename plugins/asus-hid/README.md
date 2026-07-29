---
title: Plugin: Asus HID
---

## Introduction

The ASUS HID plugin is used for interacting with the ITE MCUs on Asus
devices.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

The last 1kB of the image is a trailer describing which parts of the flash the
payload covers, and only the first of those regions is written. The rest of the
image is padding, except for the recovery bootloader that sits between the two
regions, which the device already holds and which is deliberately not shipped
in the update.

This plugin supports the following protocol ID:

* `com.asus.hid`

## GUID Generation

These devices use a DeviceInstanceId built from the hidraw vendor and product,
with the microcontroller product code appended:

* `HIDRAW\VEN_0B05&DEV_1B4C&PART_RC73XA`

In bootloader mode the part enumerates as an ITE device instead, and the
microcontroller index is appended rather than the product code:

* `HIDRAW\VEN_048D&DEV_89DC&RECOVERY_0`

## Update Behavior

The part is flashed as a whole rather than one microcontroller at a time, which
is what the vendor tool does. The child devices report the version of each
microcontroller but are not update targets themselves.

Updating detaches the part into an ITE bootloader, which takes about eight
seconds and leaves the gamepad, the keyboard and the power button unresponsive
until it comes back. The device restarts after the update.

## Vendor ID Security

The vendor ID is set from the HID vendor, in this instance set to `HIDRAW:0x0B05`

## External Interface Access

This plugin requires read/write access to `/dev/hidraw*`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

Writing firmware has been available since fwupd version `2.1.0`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### AsusHidNumMcu

The number of MCUs connected to the USB endpoint.

Since: 2.0.0
