DFU CSR Support
===============

Introduction
------------

CSR is often called “driverless DFU” and is used only by BlueCore chips from
Cambridge Silicon Radio (now owned by Qualcomm). The driverless just means that
it's DFU like, and is routed over HID.

CSR is a ODM that makes most of the Bluetooth audio chips in vendor hardware.
The hardware vendor can enable or disable features on the CSR microcontroller
depending on licensing options (for instance echo cancellation), and there’s
even a little virtual machine to do simple vendor-specific things.

All the CSR chips are updatable in-field, and most vendors issue updates to fix
sound quality issues or to add support for new protocols or devices.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
DFU file format.

This plugin supports the following protocol ID:

 * com.qualcomm.dfu

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_0A12&PID_1337&REV_2520`
 * `USB\VID_0A12&PID_1337`
 * `USB\VID_0A12`

Update Behavior
---------------

A DFU device usually presents in runtime mode (or with no interfaces defined),
but if the user puts the device into bootloader mode using a physical button
it then enumerates with a HID descriptor. On attach the device returns to
runtime mode which *may* mean the device "goes away".

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x0A12`

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
