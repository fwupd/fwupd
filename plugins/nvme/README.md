NVMe
====

Introduction
------------

This plugin adds support for NVMe storage hardware. Devices are enumerated from
the Identify Controller data structure and can be updated with appropriate
firmware file. Firmware is sent in 4kB chunks and activated on next reboot.

The device GUID is read from the vendor specific area and if not found then
generated from the trimmed model string.
