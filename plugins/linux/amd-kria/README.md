---
title: Plugin: AMD Kria
---

## Introduction

The AMD Kria plugin is used to represent the system firmware stored on QSPI
for the AMD Kria system on module device specifically when not booted using
UEFI support in U-Boot.  **When UEFI support is used the plugin will be disabled.**

It uses the devices created by the mtd plugin to discover the firmware
version and uses the known behavior of U-Boot ESP handling to distribute
updates to the device.

U-Boot will automatically pick up the firmware (so no efivars are needed) and
will also clean up the firmware after upgrade is completed.

## GUID Generation

These devices use a GUID generation scheme that reflects data stored in the
EEPROM on the board the Kria SoM is inserted into.

* `UEFI\VENDOR_XILINX`
* `UEFI\VENDOR_XILINX&PRODUCT_SMK-K26-XCL2G`

## Firmware Format

The firmware is distributed in UEFI capsule format and it's format is described
in <https://xilinx.github.io/kria-apps-docs/bootfw/build/html/docs/bootfw_overview.html>.

* `org.uefi.capsule`

## Quirks

* `AmdKriaEepromAddr` represents the I2C address for the SoM EEPROM

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Mario Limonciello: @superm1
* Michal Simek @michalsimek
