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

This plugin supports the following protocol IDs:

 * org.usb.dfu
 * com.st.dfuse

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_273F&PID_1003&REV_0001`
 * `USB\VID_273F&PID_1003`
 * `USB\VID_273F`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example `USB:0x0A12`

Quirk use
---------
This plugin uses the following plugin-specific quirks:

| Quirk                  | Description                                 | Minimum fwupd version |
|------------------------|---------------------------------------------|-----------------------|
|`DfuFlags`              | Optional quirks for a DFU device which doesn't follow the DFU 1.0 or 1.1 specification | 1.0.1|
|`DfuForceVersion`       | Forces a specific DFU version for the hardware device. This is required if the device does not set, or sets incorrectly, items in the DFU functional descriptor. |1.0.1|
