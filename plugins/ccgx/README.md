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
blobs in an undisclosed binary file format.

This plugin supports the following protocol ID:

 * com.cypress.ccgx

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_17EF&PID_A38F`
 * `USB\VID_04B4&PID_521A`
 * `USB\VID_17EF&PID_A354`
 * `USB\VID_17EF&PID_A35F`
 * `USB\VID_04B4&PID_5218`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example set to `USB:0x04b4`

Custom flag use:
----------------
This plugin uses the following plugin-specific custom flags:

* `skip-restart`: Don't run the reset or reboot procedure of the component
