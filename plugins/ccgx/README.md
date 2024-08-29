---
title: Plugin: CCGX
---

## Introduction

This plugin can flash firmware on Infineon (previously Cypress) CCGx USB-C
controller family of devices used in docks.

## Supported Protocols

This plugin supports the following protocol IDs:

* `com.cypress.ccgx` (deprecated)
* `com.infineon.ccgx`

## Device Flash

There are four kinds of flash layout. Single image firmware is not currently
supported in this plugin.

### Symmetric Firmware

In symmetric firmware topology, FW1 and FW2 are both primary (main) firmware
with identical sizes and functionality. We can only update FW1 from FW2 or FW2
from FW1. This does mean we need to update just one time as booting from either
firmware slot gives a fully functional device.

After updating the "other" firmware we can just use `CY_PD_DEVICE_RESET_CMD_SIG`
to reboot into the new firmware, and no further action is required.

### Asymmetric Firmware

In asymmetric firmware topology, FW1 is backup and FW2 is primary (main)
firmware with different firmware sizes. The backup firmware may not support all
dock functionality.

To update primary, we thus need to update twice:

Case 1: FW2 is running

* Update FW1 -> Jump to backup FW `CY_PD_JUMP_TO_ALT_FW_CMD_SIG` -> reboot
* Update FW2 -> Reset device `CY_PD_DEVICE_RESET_CMD_SIG` -> reboot -> FW2

Case 2: FW1 is running (recovery case)

* Update FW2 ->  Reset device `CY_PD_DEVICE_RESET_CMD_SIG` -> reboot -> FW2

The `CY_PD_JUMP_TO_ALT_FW_CMD_SIG` command is allowed only in asymmetric FW, but
`CY_PD_DEVICE_RESET_CMD_SIG` is allowed in both asymmetric FW and symmetric FW.

## Firmware Format

### Cyacd firmware format

The daemon will decompress the cabinet archive and extract several firmware
blobs in cyacd file format. See <https://community.cypress.com/docs/DOC-10562>
for more details.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1234&PID_5678`

Devices also have additional instance IDs which corresponds to the silicon ID,
application ID and device mode, e.g.

* `USB\VID_1234&PID_5678&SID_9ABC`
* `USB\VID_1234&PID_5678&SID_9ABC&APP_DEF1`
* `USB\VID_1234&PID_5678&SID_9ABC&APP_DEF1&MODE_FW2`

## Update Behavior

The device usually presents in runtime HID mode, but on detach re-enumerates
with with the HPI interface. On attach the device again re-enumerates back to the runtime HID mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the HPI and runtime modes are treated as the same device.

## Vendor ID Security

The vendor ID is set from the USB vendor, for example set to `USB:0x04B4`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### CcgxFlashRowSize

Set the size of the flash row in bytes, as a 32 bit integer.

Since: 1.4.0

### CcgxFlashSize

Set the maximum flash size, as a 32 bit integer.

Since: 1.4.0

### CcgxImageKind

Set the image kind from one of:

* `unknown`
* `single`
* `dual-symmetric`
* `dual-asymmetric`
* `dual-asymmetric-variable`

Since: 1.4.0

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.4.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Brent Wu: @IfxBrent
