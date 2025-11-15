---
title: Plugin: AMD PMC
---

## Introduction

This plugin reports the firmware version of a microcontroller contained in AMD Zen CPU/APUs
called the System Management Unit on kernels that support exporting this information.

## External Interface Access

This plugin requires read only access to attributes located within `/sys/bus/platform/drivers/amd_pmc`.

## Version Considerations

This plugin has been available since fwupd version `1.8.5`.
