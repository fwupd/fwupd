OptionROM Support
=================

Introduction
------------

This plugin is also able to read and parse the firmware of some PCI devices
which allows some host state verification to be done.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `PCI\VEN_%04X&DEV_%04X`

Vendor ID Security
------------------

The device is not upgradable and thus requires no vendor ID set.

External interface access
-------------------------

This plugin requires read access to the rom file of PCI devices (`/sys/class/pci_bus/*/device/rom`)
