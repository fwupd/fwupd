---
title: Plugin: Sunplus Camera
---

## Introduction

This plugin updates Sunplus/HYGD UVC cameras that expose the normal runtime
firmware update path over `video4linux` extension-unit controls.

This first implementation is intentionally limited to the healthy runtime mode:

* `USB\VID_1BCF&PID_2CED`

It does not support recovery-state devices that enumerate as `1bcf:0b09`.

## Firmware Format

The firmware payload is a raw vendor `.bin` blob.

This plugin supports the following protocol ID:

* `com.sunplus.camera`

## GUID Generation

These devices use the standard UDEV DeviceInstanceId values, e.g.

* `VIDEO4LINUX\VEN_1BCF&DEV_2CED`

## Update Behavior

The device is updated in runtime mode using UVC extension-unit controls on the
primary `video4linux` capture node for the detached camera. This is not
guaranteed to be `/dev/video0`; on systems with an integrated webcam it may be
another node such as `/dev/video4`.

The normal update flow is:

* enable ISP/download mode using selector `9`
* clear ASIC register `0x2501` if the previous download-state value is nonzero
* stream the raw payload using selector `10`
* compare the XOR checksum using selector `11`
* finalize the transfer using selector `12`
* verify the programmed contents using selector `14` + selector `22`
* trigger a vendor soft reset using ASIC register writes over selectors `2` and `3`

## External Interface Access

This plugin requires read/write access to the primary `video4linux` node for
the detached camera.
This plugin requires the `UVCIOC_CTRL_QUERY` ioctl interface.
