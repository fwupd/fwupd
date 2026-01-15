---
title: Plugin: HP BIOSCFG
---

## Introduction

This allows checking whether the firmware on an HP system is configured to
allow UEFI capsule updates using the hp-bioscfg kernel module.

## External Interface Access

This plugin requires:

* read access to `/sys/class/firmware-attributes`.

## Version Considerations

This plugin has been available since fwupd version `1.6.2`.
