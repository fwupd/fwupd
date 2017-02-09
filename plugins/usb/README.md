USB Support
===========

Introduction
------------

This plugin reads the version numbers of USB devices. It cannot deploy firmware
onto devices but is used to list devices with known firmware updates that may
require booting into another operating system to apply.

As other plugins may be able to actually deploy firmware onto the same device
that this plugin can handle the signal emission is delayed to allow the better
plugin to "win" and own the device. It is therefor important that the other
plugin finishes enumeration of the device in less than 500ms.
