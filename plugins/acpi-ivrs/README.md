# DMA Protection

## Introduction

This plugin checks if Pre-boot DMA remapping is available and enabled from the [ACPI IVRS](http://support.amd.com/TechDocs/48882_IOMMU.pdf) table.
The result will be stored in an security attribute for HSI.

## External Interface Access

This plugin requires read access to `/sys/firmware/acpi/tables`.
