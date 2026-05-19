---
title: Plugin: ST Bootloader (STM32)
---

## Introduction

The ST I2C bootloader is documented in [AN4221](http://www.st.com/web/en/resource/technical/document/application_note/DM00072315.pdf).

The boot mode selection for STM32 is documented in ST application note [AN2606](http://www.st.com/web/en/resource/technical/document/application_note/CD00167594.pdf).

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a packed binary file format.

This plugin supports the following protocol ID:

* `com.st.stm32`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_273F&PID_1001`

Once the STM32 device has been probed an additional instance PID is added which sets up the
various per-chip parameters like SRAM address:

* `STM32\CID_0472`

## Update Behavior

The device is updated by bla bla bla.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x273F`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### StStm32SramAddr

The SRAM section start address

Since: 2.1.6

### StStm32SramLen

The SRAM section length.

Since: 2.1.6

### StStm32FlashAddr

The flash section address.

Since: 2.1.6

### StStm32FlashLen

The flash section length.

Since: 2.1.6

### StStm32OptionAddr

The option section address.

Since: 2.1.6

### StStm32OptionLen

The option section length.

Since: 2.1.6

### StStm32MemAddr

The memory section address.

Since: 2.1.6

### StStm32MemLen

The memory section length.

Since: 2.1.6

### StStm32PagesPerSector

The number of pages per sector.

Since: 2.1.6

### StStm32PageSize

The page size, split by commas if the page sizes are not all the same.

Since: 2.1.6

### Flags=no-mass-erase

The device does not support mass erasing.

Since: 2.1.6

### Flags=clear-pempty

The device requires pempty to be cleared.

Since: 2.1.6

### Flags=obl-launch-required

The device required OBL_LAUNCH.

Since: 2.1.6

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.1.6`.
