---
title: Plugin: MTD Recovery
---

This plugin allows a system without an exported MTD device to be updated by matching the DMI
details of the platform. This allows updating SPI devices that are hidden behind a MUX which is
very common when running fwupd on a BMC.

This plugin supports the following protocol ID:

* `org.infradead.mtd`

## GUID Generation

These devices are generated from the baseboard vendor ID:

* `MTD\VEN_ASPEED&DEV_AST2500EVB`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### MtdRecoveryGpioNumber

Optional GPIO device number to use as a MUX, e.g. `123`.

Since: 1.8.9

### MtdRecoveryKernelDriver

Kernel driver name, e.g. `npcm-fiu`.

Since: 1.8.9

### MtdRecoveryBindId

Optional kernel ID for binding the device, e.g. `10000000.spi`.

Since: 1.8.9

## Update Behavior

On detach the platform driver for the MTD device is loaded, optionally also using a GPIO MUX.
The discovered MTD device is used as a proxy and the image it written, then on detach the MUX is
optionally disabled and the MTD device unloaded.

To test this plugin you can use a quirk file with the following contents:

    # a GUID from `fwupdtool hwids`
    [536c0c4c-e739-5bc7-8f93-042c73d961ef]
    Plugin = mtd_recovery

    [MTD\VEN_LENOVO&DEV_20TJS2F44A]
    MtdRecoveryGpioNumber = 123
    MtdRecoveryKernelDriver = modulename
    MtdRecoveryBindId = device.spi

## Vendor ID Security

The vendor ID is set from the baseboard vendor, for example `DMI:ASPEED`

## External Interface Access

This plugin requires read/write access to `/dev/mtd`.
