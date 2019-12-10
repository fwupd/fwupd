Fastboot Support
================

Introduction
------------

This plugin is used to update hardware that uses the fastboot protocol.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
ZIP file format. Inside the zip file must be all the firmware images for each
partition and a manifest file. The partition images can be in any format, but
the manifest must be either an Android `flashfile.xml` format file, or a QFIL
`partition_nand.xml` format file.

For both types, all partitions with a defined image found in the zip file will
be updated.

This plugin supports the following protocol ID:

 * com.google.fastboot

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_18D1&PID_4EE0&REV_0001`
 * `USB\VID_18D1&PID_4EE0`
 * `USB\VID_18D1`

Quirk use
---------
This plugin uses the following plugin-specific quirk:

| Quirk                  | Description                      | Minimum fwupd version |
|------------------------|----------------------------------|-----------------------|
| `FastbootBlockSize`    | Block size to use for transfers  | 1.2.2                 |

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example `USB:0x18D1`
