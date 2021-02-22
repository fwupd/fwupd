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

Update Behavior
---------------

The device usually presents in runtime mode, but on detach re-enumerates with a
different USB VID and PID in DFU APP mode. The device is then further detached
by the `dfu` plugin.

On DFU attach the device again re-enumerates back to the Jabra runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0A12`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
