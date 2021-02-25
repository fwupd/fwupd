ATA
===

Introduction
------------

This plugin allows updating ATA/ATAPI storage hardware. Devices are enumerated
from the block devices and if ID_ATA_DOWNLOAD_MICROCODE is supported they can
be updated with appropriate firmware file.

Updating ATA devices is more dangerous than other hardware such as DFU or NVMe
and should be tested carefully with the help of the drive vendor.

The device GUID is read from the trimmed model string.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

 * org.t13.ata

GUID Generation
---------------

These device use the Microsoft DeviceInstanceId values, e.g.

 * `IDE\VENDOR[40]REVISION[8]`
 * `IDE\0VENDOR[40]`

See https://docs.microsoft.com/en-us/windows-hardware/drivers/install/identifiers-for-ide-devices
for more details.

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, but it is
only activated when the system is in the final shutdown stages. This is done to
minimize the chance of data loss if the switch to the new firmware is not done
correctly.

Vendor ID Security
------------------

No vendor ID is set as there is no vendor field in the IDENTIFY response.

Quirk use
---------
This plugin uses the following plugin-specific quirks:

| Quirk                  | Description                               | Minimum fwupd version |
|------------------------|-------------------------------------------|-----------------------|
| `AtaTransferBlocks`    | Blocks to transfer, or `0xffff` for max   | 1.2.4                 |
| `AtaTransferMode`      | The transfer mode, `0x3`, `0x7` or `0xe`  | 1.2.4                 |

External interface access
-------------------------
This plugin requires the `SG_IO` ioctl interface.
