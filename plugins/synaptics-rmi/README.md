Synaptics RMI4 Support
======================

Introduction
------------

This plugin updates integrated Synaptics RMI4 devices, typically touchpads.

GUID Generation
---------------

The HID DeviceInstanceId values are used, e.g. `HIDRAW\VEN_06CB&DEV_4875`.

These devices also use custom GUID values constructed using the board ID, e.g.

 * `SYNAPTICS_RMI\TM3038-002`
 * `SYNAPTICS_RMI\TM3038`


Vendor ID Security
------------------

The vendor ID is set from the udev vendor, in this instance set to `HIDRAW:0x06CB`

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a proprietary (but docucumented) file format.

This plugin supports the following protocol ID:

 * com.synaptics.rmi
