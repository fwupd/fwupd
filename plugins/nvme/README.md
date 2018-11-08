NVMe
====

Introduction
------------

This plugin adds support for NVMe storage hardware. Devices are enumerated from
the Identify Controller data structure and can be updated with appropriate
firmware file. Firmware is sent in 4kB chunks and activated on next reboot.

The device GUID is read from the vendor specific area and if not found then
generated from the trimmed model string.

GUID Generation
---------------

These device use the NVMe DeviceInstanceId values, e.g.

 * `NVME\VEN_1179&DEV_010F&REV_01`
 * `NVME\VEN_1179&DEV_010F`
 * `NVME\VEN_1179`

Additionally, for NVMe drives with Dell vendor firmware two extra GUIDs are
added:

 * `STORAGE-DELL-${component-id}`

and any optional GUID saved in the vendor extension block.
