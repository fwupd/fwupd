---
title: Plugin: CCGX
---

## Introduction

This plugin can flash firmware on Infineon (previously Cypress) CCGx USB-C
controller family of devices used in docks.

## Supported Protocols

This plugin supports the following protocol IDs:

* `com.cypress.ccgx` (deprecated)
* `com.cypress.ccgx.dmc` (deprecated)
* `com.infineon.ccgx`
* `com.infineon.ccgx.dmc`

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

### DMC(Dock Management Controller) Composite Firmware

In composite firmware topology, a single firmware image contains metadata and
firmware images of multiple devices including DMC itself in a dock system.

### DMC Factory Mode

Dock Management Controller devices have a *composite version* that is used to
describe the dock hardware as a whole, rather than enumerating and updating
each sub-component separately.
When OEMs have not followed the IHV-approved factory assembly process, the
composite number is unset and fwupd would display `0.0.0.0` in the GUI and
on the command line.

In fwupd >= 1.8.11 we detect if the device is in *factory mode* and set the
version number to `0.0.0.1`.
When the device is in factory mode any valid upgrade will be allowed, which
means the user might be prompted to “update to” the same current version
installed on the dock.
For millions of devices this is both a waste of time, resources, and also
would inconvenience the user with an additional process for no reason.

For devices that have been shipped in factory mode, but would like to avoid
the update from `0.0.0.1` to the original version on the LVFS, can add a
quirk entry which matches the `devx` subcomponent *base version*.
In this example we match the parent VID, PID, the ComponentID and the `devx`
base firmware version, setting the parent composite version to `0.0.0.15`.

    [USB\VID_2188&PID_0035&CID_05&VER_3.3.1.69]
    CcgxDmcCompositeVersion = 15

All the `devx` subcomponent versions can be shown on the console using:

    sudo fwupdtool –plugins ccgx get-devices –show-all –verbose

## Firmware Format

There are two kinds of firmware format.

### Cyacd firmware format

The daemon will decompress the cabinet archive and extract several firmware
blobs in cyacd file format. See <https://community.cypress.com/docs/DOC-10562>
for more details.

### DMC composite firmware format

The daemon will decompress the cabinet archive and extract several firmware
blobs in a combined image file format. See 4.4.1 Single Composite
(Combined) Dock Image at <https://www.cypress.com/file/387471/download>
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
with with a DMC or HPI interface. On attach the device again re-enumerates
back to the runtime HID mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the DMC/HPI and runtime modes are treated as the same device.

## Vendor ID Security

The vendor ID is set from the USB vendor, for example set to `USB:0x04B4`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### CcgxDmcTriggerCode

DMC devices need a specified trigger code to request the device to update
the firmware and the trigger code depends on the devices.

    0x0: Do not update
    0x1: Update immediately
    0x2: Update after port disconnected

Since: 1.8.0

### CcgxDmcCompositeVersion

Set the parent composite version, as a 32 bit integer.

Since: 1.8.11

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
