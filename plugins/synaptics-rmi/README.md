Synaptics RMI4 Support
======================

Introduction
------------

This plugin updates integrated Synaptics RMI4 devices, typically touchpads.

GUID Generation
---------------

The HID DeviceInstanceId values are used, e.g. `HIDRAW\VEN_06CB&DEV_4875`.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a proprietary (but docucumented) file format.

This plugin supports the following protocol ID:

 * com.synaptics.rmi
