OptionROM Support
=================

Introduction
------------

This plugin reads the version numbers of PCI devices. It cannot deploy firmware
onto devices but is used to list devices with known firmware updates that may
require booting into another operating system to apply.

This plugin is also able to read and parse the firmware of some PCI devices
which allows some host state verification to be done.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `PCI\VEN_%04X&DEV_%04X`

Additionally, GUIDs found in OptionROMs may also be added.

Vendor ID Security
------------------

The device is not upgradable and thus requires no vendor ID set.
