Chrome OS EC Support
===================

Introduction
------------

This plugin provides support for the firmware updates for Chrome OS EC
project based devices.

Initially, it supports the USB endpoint updater, but lays the groundwork for
future updaters which use other update methods other than the USB endpoint.

This is based on the chromeos ec project's usb_updater2 application [1].

Information about the USB update protocol is available at [2].

Firmware Format
---------------
The daemon will decompress the cabinet archive and extract a firmware blob in
the Google fmap [3] file format.

This plugin supports the following protocol ID:

 * com.google.usb.crosec

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_18D1&PID_501A`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, which is set to various different
values depending on the model and device mode. The list of USB VIDs used is:

 * `USB:0x18D1`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.

[1] https://chromium.googlesource.com/chromiumos/platform/ec/+/master/extra/usb_updater/usb_updater2.c
[2] https://chromium.googlesource.com/chromiumos/platform/ec/+/master/docs/usb_updater.md
[3] https://www.chromium.org/chromium-os/firmware-porting-guide/fmap
