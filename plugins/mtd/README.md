---
title: Plugin: MTD
---

## Introduction

The Memory Technology Device (MTD) interface is a way of abstracting flash devices as if they were
normal block devices.

See <http://www.linux-mtd.infradead.org/doc/general.html> for more details.

This plugin supports the following protocol ID:

* `org.infradead.mtd`

## GUID Generation

These devices use custom DeviceInstanceId values built from the device `NAME` and DMI data, e.g.

* `MTD\NAME_Factory`
* `MTD\VENDOR_foo&NAME_baz`
* `MTD\VENDOR_foo&PRODUCT_bar&NAME_baz`

If the `FirmwareGType` quirk is set for the device then the firmware is read back from the device at
daemon startup and parsed for the version number.
In the event the firmware has multiple child images then the device GUIDs are used as firmware IDs.

## Update Behavior

The MTD device is erased in chunks, written and then read back to verify.

Although fwupd can read and write a raw image to the MTD partition there is no automatic way to
get the *existing* version number. By providing the `GType` fwupd can read the MTD partition and
discover additional metadata about the image. For instance, adding a quirk like:

    [MTD\VENDOR_PINE64&PRODUCT_PinePhone-Pro&NAME_spi1.0]
    FirmwareGType = FuUswidFirmware

... and then append or insert the image into the MTD image with prepared SBoM metadata:

    pip install uswid
    uswid --load uswid.ini --save metadata.uswid

This would allow fwupd to read the MTD image data, look for a [uSWID](https://github.com/hughsie/python-uswid)
data section and then parse the metadata from that. Any of the firmware formats supported by
`fwupdtool get-firmware-gtypes` that can provide a version can be used.

## Vendor ID Security

The vendor ID is set from the system vendor, for example `DMI:LENOVO`

## External Interface Access

This plugin requires read/write access to `/dev/mtd`.

## Version Considerations

This plugin has been available since fwupd version `1.7.2`.
