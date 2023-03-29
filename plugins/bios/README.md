---
title: Plugin: BIOS
---

## Introduction

This plugin checks UEFI capsules are available, and if missing a HSI failure is reported.

## External Interface Access

This plugin requires read only access to attributes located within `/sys/firmware/efi/esrt`.

## Version Considerations

This plugin has been available since fwupd version `1.5.0`.
