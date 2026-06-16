# Coreboot CFR

## Introduction

This plugin exposes coreboot CFR settings that publish runtime apply metadata.
The coreboot CFR documentation is available at <https://doc.coreboot.org/drivers/cfr.html>.

The plugin reads the `CorebootCfrSettings` EFI variable and registers settings that include
runtime apply metadata. Settings without a runtime apply hook are ignored.

## Update Behavior

When a supported setting is changed, the plugin writes the matching CFR EFI variable and then
triggers the firmware runtime apply hook. The current implementation supports the `APM_CNT` apply
method used by the StarFighter keyboard-backlight timeout setting.

## External Interface Access

This plugin reads and writes EFI variables using the coreboot CFR GUID. For `APM_CNT` settings it
also opens `/dev/port` and writes the firmware-provided runtime apply ID to the APM status and
control ports to trigger the SMI handler.
