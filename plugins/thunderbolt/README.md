Thunderbolt™ Support
====================

Introduction
------------

Thunderbolt™ is the brand name of a hardware interface developed by Intel that
allows the connection of external peripherals to a computer.
Versions 1 and 2 use the same connector as Mini DisplayPort (MDP), whereas
version 3 uses USB Type-C.

Build Requirements
------------------

For UEFI capsule support, you need to install libtbtfwu.
* source:		https://github.com/01org/thunderbolt-software-user-space/tree/fwupdate

If you don't want or need this functionality you can use the
`--disable-thunderbolt` option.
