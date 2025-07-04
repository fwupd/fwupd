---
title: Plugin: ACPI FACP — Fixed ACPI Description Table
---

## Introduction

This plugin checks if S2I sleep is available. The result will be stored in an
security attribute for HSI.

## External Interface Access

This plugin requires read access to `/sys/firmware/acpi/tables`.

## Version Considerations

This plugin has been available since fwupd version `1.5.0`.
