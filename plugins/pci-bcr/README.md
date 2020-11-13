PCI BIOS Control Register
=========================

Introduction
------------

This plugin checks if the system SPI chip is locked. The result will be stored
in an security attribute for HSI.

External interface access
-------------------------
This plugin requires read access to the config space of PCI devices (`/sys/class/pci_bus/*/device/config`)
