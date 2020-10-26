PCI MEI
=======

Introduction
------------

This plugin checks if the ME is in Manufacturing Mode. The result will be stored
in an security attribute for HSI.

External interface access
-------------------------
This plugin requires read access to the config space of PCI devices (`/sys/class/pci_bus/*/device/config`)
