# Intel USB4

## Introduction

This plugin supports the Goshen Ridge hardware which is a USB-4 controller from Intel.
These devices can updated using multiple interfaces, but this plugin only uses the XHCI interface.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, with vendor specific header.

This plugin supports the following protocol ID:

* com.intel.thunderbolt

## GUID Generation

These devices use a custom generation scheme, which is quite intentionally identical to thunderbolt
plugin:

* `TBT-{nvm_vendor_id}{nvm_product_id}`

## Update Behavior

All devices will be updated the next time the USB-C plug from the dock is unplugged from the host.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x8087`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
