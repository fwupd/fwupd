Elan TouchPad
=============

Introduction
------------

This plugin allows updating Touchpad devices from Elan. Devices are enumerated
using HID and raw I²C nodes. The I²C mode is used for firmware recovery.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

 * tw.com.emc.elantp

GUID Generation
---------------

These device uses the standard DeviceInstanceId values, e.g.

 * `HIDRAW\VEN_04F3&DEV_3010`

Additionally another instance ID is added which corresponds to the module ID:

 * `HIDRAW\VEN_04F3&DEV_3010&MOD_1234`

These devices also use custom GUID values for the IC configuration, e.g.

 * `ELANTP\ICTYPE_09`

 Additionally another instance ID is added which corresponds to the IC type & module ID:

 * `ELANTP\ICTYPE_09&MOD_1234`

Update Behavior
---------------

The device usually presents in HID mode, and the firmware is written to the
device by switching to a IAP mode where the touchpad is nonfunctional.
Once complete the device is reset to get out of IAP mode and to load the new
firmware version.

On flash failure the device is nonfunctional, but is recoverable by writing
to the i2c device. This is typically much slower than updating the device
using HID and also requires a model-specific HWID quirk to match.

Vendor ID Security
------------------

The vendor ID is set from the HID vendor, for example set to `HIDRAW:0x17EF`

Quirk use
---------

This plugin uses the following plugin-specific quirks:

| Quirk                  | Description                               | Minimum fwupd version |
|------------------------|-------------------------------------------|-----------------------|
| `ElantpIcPageCount`    | The IC page count                         | 1.4.6                 |
| `ElantpIapPassword`    | The IAP password                          | 1.4.6                 |

External interface access
-------------------------
This plugin requires ioctl access to `HIDIOCSFEATURE` and `HIDIOCGFEATURE`.
