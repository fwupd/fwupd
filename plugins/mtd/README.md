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

## Update Behavior

The MTD device is erased in chunks, written and then read back to verify.

## Vendor ID Security

The vendor ID is set from the system vendor, for example `DMI:LENOVO`

## External Interface Access

This plugin requires read/write access to `/dev/mtd`.
