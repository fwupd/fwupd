---
title: Plugin: ACPI SBOM - Software Bill of Materials
---

## Introduction

The SBOM is an ACPI table where a platform can expose SBOM entries that document connected hardware.

These elements are typically system hardware components such as CSME and EC firmware.

The daemon saves these SBOM reports in uncompressed format to the `/var/lib/fwupd/sbom` directory
for front-end software to parse and display.

## External Interface Access

This plugin requires read access to `/sys/firmware/acpi/tables`.

## Version Considerations

This plugin has been available since fwupd version `2.1.3`.
