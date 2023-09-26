---
title: Plugin: UEFI ESRT
---

## Introduction

This allows enabling the BIOS setup option for UEFI capsule updates without
manually going into BIOS setup.

## External Interface Access

This plugin requires:

* read/write access to `/sys/class/firmware-attributes`

## Version Considerations

This plugin has been available since fwupd version `1.9.6`.
