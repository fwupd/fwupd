SPI
===

Introduction
------------

This plugin verifies the SPI contents, typically an Intel Flash descriptor.
The result will be stored in an security attribute for HSI.

External interface access
-------------------------
This plugin requires read access to `/dev/port` and thus will not work if the
kernel is locked down.
