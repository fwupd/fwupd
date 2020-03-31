Cypress support
===============

Introduction
------------
This plugin can flash firmware on Cypress CCGx USB-C controller family of
devices used in dock solutions.

Supported devices:

 * Lenovo Gen2 Dock
 * Lenovo Hybrid Dock

Device Flash
============

There are three kinds of flash layout. Single image firmware is not currently
supported in this plugin.

Symmetric Firmware
------------------

In symmetric firmware topology, FW1 and FW2 are both primary (main) firmware
with identical sizes and functionality. We can only update FW1 from FW2 or FW2
from FW1. This does mean we need to update just one time as booting from either
firmware slot gives a fully functional device.

After updating the "other" firmware we can just use `CY_PD_DEVICE_RESET_CMD_SIG`
to reboot into the new firmware, and no further action is required.

Asymmetric Firmware
-------------------

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

Firmware Format
---------------
The daemon will decompress the cabinet archive and extract several firmware
blobs in cyacd file format. See https://community.cypress.com/docs/DOC-10562
for more details.

This plugin supports the following protocol ID:

 * com.cypress.ccgx

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_17EF&PID_A38F`

They additionally add other instance IDs which corresponds to the silicon ID,
application ID and device mode, e.g.

 * `USB\VID_17EF&PID_A38F&SID_1234`
 * `USB\VID_17EF&PID_A38F&SID_1234&APP_5678`
 * `USB\VID_17EF&PID_A38F&SID_1234&APP_5678&MODE_FW2`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example set to `USB:0x17EF`
