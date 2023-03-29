---
title: Plugin: ACPI DMAR — DMA Protection
---

## Introduction

This plugin checks if DMA remapping for Thunderbolt devices is available. The
result will be stored in an security attribute for HSI.

## External Interface Access

This plugin requires read access to `/sys/firmware/acpi/tables`.

## Version Considerations

This plugin has been available since fwupd version `1.5.0`.
