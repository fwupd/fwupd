8Bitdo Support
==============

Introduction
------------

This plugin can flash the firmware on the 8Bitdo game pads.

Ebitdo support is supported directly by this project with the embedded libebitdo
library and is possible thanks to the vendor open sourcing the flashing tool.

The 8Bitdo devices share legacy USB VID/PIDs with other projects and so we have
to be a bit careful to not claim other devices as our own.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format. The binary file has a vendor-specific header
that is used when flashing the image.

This plugin supports the following protocol ID:

 * com.8bitdo

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_2DC8&PID_AB11&REV_0001`
 * `USB\VID_2DC8&PID_AB11`
 * `USB\VID_2DC8`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, which is set to various different
values depending on the model and device mode. The list of USB VIDs used is:

 * `USB:0x2DC8`
 * `USB:0x0483`
 * `USB:0x1002`
 * `USB:0x1235`
 * `USB:0x2002`
 * `USB:0x8000`
