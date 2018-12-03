DFU Support
===========

Introduction
------------

Device Firmware Update is a standard that allows USB devices to be easily and
safely updated by any operating system.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
DFU or DfuSe file format.

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_273F&PID_1003&REV_0001`
 * `USB\VID_273F&PID_1003`
 * `USB\VID_273F`

Quirk use
---------
This plugin uses the following plugin-specific quirks:

| Quirk                  | Description                                 | Minimum fwupd version |
|------------------------|---------------------------------------------|-----------------------|
|`DfuFlags`              | Optional quirks for a DFU device which doesn't follow the DFU 1.0 or 1.1 specification | 1.0.1|
|`DfuForceVersion`       | Forces a specific DFU version for the hardware device. This is required if the device does not set, or sets incorrectly, items in the DFU functional descriptor. |1.0.1|
|`DfuJabraDetach`        | Assigns the two magic bytes sent to the Jabra hardware when the device is in runtime mode to make it switch into DFU mode.|1.0.1|
