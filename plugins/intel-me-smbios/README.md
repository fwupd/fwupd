---
title: Plugin: Intel ME (SMBIOS)
---

## Introduction

The ME is a co-processor used by Intel, with the latest versions known as CSME.

This plugin checks if the ME is set up correctly. The result will be stored as HSI security attributes.

## External Interface Access

This plugin requires read/write access to `/sys/firmware/dmi/tables/DMI`.

## Version Considerations

This plugin has been available since fwupd version `2.1.1`.
