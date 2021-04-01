Analogix
========

Introduction
------------

This plugin can flash the firmware of some Analogix billboard devices.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a Intel Hex file format. The resulting binary image is either:

 * `OCM` section only
 * `CUSTOM` section only
 * Multiple sections excluded `CUSTOM` -- at fixed offsets and sizes

This plugin supports the following protocol ID:

 * com.analogix.bb

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_1F29&PID_7518`

Update Behavior
---------------

The device is updated at runtime using USB control transfers.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor. The list of USB VIDs used is:

 * `USB:0x1F29`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
