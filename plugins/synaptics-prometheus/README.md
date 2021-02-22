Synaptics Prometheus
====================

Introduction
------------

This plugin can flash the firmware on the Synaptics Prometheus fingerprint readers.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format. The binary file has a vendor-specific header
that is used when flashing the image.

This plugin supports the following protocol ID:

 * com.synaptics.prometheus

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_06CB&PID_00A9&REV_0001`
 * `USB\VID_06CB&PID_00A9`
 * `USB\VID_06CB&PID_00A9-cfg`
 * `USB\VID_06CB&PID_00A9&CFG1_3483&CFG2_500`

Update Behavior
---------------

The device usually presents in runtime mode, but on detach re-enumerates with
the same USB PID in an unlocked mode. On attach the device again re-enumerates
back to the runtime locked mode.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x06CB`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
