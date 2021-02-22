Logitech HID Support
================

Introduction
------------

This plugin can flash the firmware on Logitech Unifying dongles, both the
Nordic (U0007) device and the Texas Instruments (U0008) version.

This plugin will not work with the different "Nano" dongle (U0010) as it does
not use the Unifying protocol.

Some bootloader protocol information was taken from the Mousejack[1] project,
specifically logitech-usb-restore.py and unifying.py. Other documentation was
supplied by Logitech.

Additional constants were taken from the Solaar[2] project.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a vendor-specific format that appears to be a subset of the Intel HEX format.

This plugin supports the following protocol IDs:

 * com.logitech.unifying
 * com.logitech.unifyingsigned

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values when in DFU mode:

 * `USB\VID_046D&PID_AAAA&REV_0001`
 * `USB\VID_046D&PID_AAAA`
 * `USB\VID_046D`

When in runtime mode, the HID raw DeviceInstanceId values are used:

 * `HIDRAW\VEN_046D&DEV_C52B`
 * `HIDRAW\VEN_046D`

Vendor ID Security
------------------

The vendor ID is set from the vendor ID, in this instance set to `USB:0x046D`
in bootloader and `HIDRAW:0x046D` in runtime mode.

Update Behavior
---------------

The peripheral firmware is deployed when the device is in normal runtime mode,
and the device will reset when the new firmware has been written.

The reciever device presents in runtime mode, but on detach re-enumerates with a
different USB PID in a bootloader mode. On attach the device again re-enumerates
back to the runtime mode. All unifying devices attached to the reciever will not
work for the duration of the update.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

Design Notes
------------

When a dongle is detected in bootloader mode we detach the hidraw driver from
the kernel and use raw control transfers. This ensures that we don't accidentally
corrupt the uploading firmware. For application firmware we use hidraw which
means the hardware keeps working while probing, and also allows us to detect
paired devices.

[1] https://www.mousejack.com/
[2] https://pwr-Solaar.github.io/Solaar/

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
