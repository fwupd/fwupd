Jabra Support
=============

Introduction
------------

This plugin is used to detach the Jabra device to DFU mode.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_0B0E&PID_0412`

Quirk use
---------

This plugin uses the following plugin-specific quirks:

| Quirk         | Description                                  | fwupd version |
|---------------|----------------------------------------------|---------------|
|`JabraMagic`   | Two magic bytes sent to detach into DFU mode.|1.3.3          |

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0A12`
