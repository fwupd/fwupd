---
title: Plugin: Linux Kernel Sleep
---

## Introduction

This plugin checks if s3 sleep is available. The result will be stored in an
security attribute for HSI.

## External Interface Access

This plugin requires read access to `/sys/power/mem_sleep`.

## Version Considerations

This plugin has been available since fwupd version `1.5.0`.
