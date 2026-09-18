---
title: Plugin: Linux Firmware Attributes
---

## Introduction

This plugin allows reading and changing firmware attributes.

If any `pending_reboot` sysfs attribute is set to `1` then the "pending reboot" flag will
also be set in the daemon.

## External Interface Access

This plugin requires read and write access to `/sys/class/firmware-attributes/`.

## Version Considerations

This plugin has been available since fwupd version `2.1.8`.
