SMBIOS Memory
=============

Introduction
------------

This plugin checks if SMBIOS indicates that memory error correction (ECC) is
enabled.

Memory without ECC is easy to attack using techniques like RowHamer.

The result will be stored in a security attribute for HSI.

External interface access
-------------------------
This plugin requires read access to `/sys/firmware/dmi/tables/DMI`.
