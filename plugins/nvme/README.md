NVMe
====

Introduction
------------

This plugin adds support for NVMe storage hardware. Devices are enumerated from
the Identify Controller data structure and can be updated with appropriate
firmware file. Firmware is sent in 4kB chunks and activated on next reboot.

The GUID is currently generated from the trimmed model string.
