# MTD

## Introduction

The Memory Technology Device (MTD) interface is a way of abstracting flash devices as if they were
normal block devices.

See <http://www.linux-mtd.infradead.org/doc/general.html> for more details.

This plugin supports the following protocol ID:

* org.infradead.mtd

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

## Vendor ID Security

The vendor ID is set from the system vendor, for example `DMI:LENOVO`

## External Interface Access

This plugin requires read/write access to `/dev/mtd`.
