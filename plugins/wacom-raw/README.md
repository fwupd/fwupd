Wacom RAW Support
=================

Introduction
------------

This plugin updates integrated Wacom AES and EMR devices. They are typically
connected using I²C and not USB.

GUID Generation
---------------

The HID DeviceInstanceId values are used, e.g. `HIDRAW\VEN_056A&DEV_4875`.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
Intel HEX file format.

This plugin supports the following protocol ID:

 * com.wacom.raw

Quirk use
---------
This plugin uses the following plugin-specific quirks:

| Quirk                   | Description                         | Minimum fwupd version |
|-------------------------|-------------------------------------|-----------------------|
| `WacomI2cFlashBlockSize`| Block size to transfer firmware     | 1.2.4                 |
| `WacomI2cFlashBaseAddr` | Base address for firmware           | 1.2.4                 |
| `WacomI2cFlashSize`     | Maximum size of the firmware zone   | 1.2.4                 |

Update Behavior
---------------

The device usually presents in runtime mode, but on detach re-enumerates with a
different HIDRAW PID in a bootloader mode. On attach the device re-enumerates
back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

Vendor ID Security
------------------

The vendor ID is set from the udev vendor, in this instance set to `HIDRAW:0x056A`

External interface access
-------------------------
This plugin requires ioctl `HIDIOCSFEATURE` access.
