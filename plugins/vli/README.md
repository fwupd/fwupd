VIA Support
===========

Introduction
------------

This plugin is used to update USB hubs from VIA.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an undisclosed binary file format.

This plugin supports the following protocol ID:

 * com.vli.i2c
 * com.vli.pd
 * com.vli.usbhub

GUID Generation
---------------

These devices use the standard USB DeviceInstanceId values, e.g.

 * `USB\VID_17EF&PID_3083&REV_0001`
 * `USB\VID_17EF&PID_3083`
 * `USB\VID_17EF`

All VLI devices also use custom GUID values for the device type, e.g.

 * `USB\VID_17EF&PID_3083&DEV_VL812B3`

These devices also use custom GUID values for the SPI flash configuration, e.g.

 * `VLI_USBHUB\SPI_37303840`
 * `VLI_USBHUB\SPI_3730`
 * `VLI_USBHUB\SPI_37`

Optional PD child devices sharing the SPI flash use two extra GUIDs, e.g.

 * `USB\VID_17EF&PID_3083&DEV_VL102`
 * `USB\VID_17EF&PID_3083&APP_26`

Optional I²C child devices use just one extra GUID, e.g.

 * `USB\VID_17EF&PID_3083&I2C_MSP430`
 * `USB\VID_17EF&PID_3083&I2C_PS186`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example set to `USB:0x2109`

Quirk Use
---------

This plugin uses the following plugin-specific quirks:

| Quirk                      | Description                      | Minimum fwupd version |
|----------------------------|----------------------------------|-----------------------|
| `VliDeviceKind`            | Device kind, e.g. `VL102`        | 1.3.7                 |
| `VliSpiAutoDetect`         | SPI autodetect (default 0x1)     | 1.3.7                 |
| `VliSpiCmdChipErase`       | Flash command to erase chip      | 1.3.3                 |
| `VliSpiCmdChipErase`       | Flash command to erase sector    | 1.3.3                 |
| `VliSpiCmdReadId`          | Flash command to read the ID     | 1.3.3                 |
| `VliSpiCmdReadIdSz`        | Size of the ReadId response      | 1.3.3                 |

The `VliSpiCmdReadId` and `VliSpiCmdReadIdSz` quirks have to be assigned to the device
instance attribute, rather then the flash part as the ID is required to query
the other flash chip parameters. For example:

    [USB\VID_2109&PID_0210]
    Plugin = vli
    GType = FuVliUsbhubDevice
    VliSpiCmdReadId = 0xf8
    VliSpiCmdReadIdSz = 4

    # W3IRDFLASHxxx
    [VLI_USBHUB\\SPI_37303840]
    VliSpiCmdChipErase = 0xc7
    VliSpiCmdSectorErase = 0x20

External interface access
-------------------------
This plugin requires read/write access to `/dev/bus/usb`.
