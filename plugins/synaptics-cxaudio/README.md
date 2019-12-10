Conexant Audio Support
======================

Introduction
------------

This plugin is used to update a small subset of Conexant (now owned by Synaptics)
audio devices.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
a modified SREC file format.

This plugin supports the following protocol ID:

 * com.synaptics.synaptics-cxaudio

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_17EF&PID_3083&REV_0001`
 * `USB\VID_17EF&PID_3083`
 * `USB\VID_17EF`

These devices also use custom GUID values, e.g.

 * `SYNAPTICS_CXAUDIO\CX2198X`
 * `SYNAPTICS_CXAUDIO\CX21985`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x17EF`

Quirk Use
---------

This plugin uses the following plugin-specific quirks:

| Quirk                      | Description                      | Minimum fwupd version |
|----------------------------|----------------------------------|-----------------------|
| `ChipIdBase`               | Base integer for ChipID          | 1.3.2                 |
| `IsSoftwareResetSupported` | If the chip supports self-reset  | 1.3.2                 |
| `EepromPatchValidAddr`     | Address of patch location #1     | 1.3.2                 |
| `EepromPatch2ValidAddr`    | Address of patch location #2     | 1.3.2                 |
