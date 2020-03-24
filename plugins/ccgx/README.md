Cypress support
===============

Introduction
------------
This plugin can flash firmware on Cypress CCGx USB-C controller family of
devices used in dock solutions.

Supported devices:

 * Lenovo Gen2 Dock
 * Lenovo Hybrid Dock

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

They additionally add one InstanceId which corresponds to the device mode, e.g.

 * `USB\VID_17EF&PID_A38F&MODE_BOOT`
 * `USB\VID_17EF&PID_A38F&MODE_FW1`
 * `USB\VID_17EF&PID_A38F&MODE_FW2`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example set to `USB:0x17EF`
