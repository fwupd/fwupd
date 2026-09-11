---
title: Plugin: AMD Firmware Configuration
---

## Introduction

This plugin exposes BIOS settings described by AMD's `AmdFwCfg` ACPI tables.
It parses the embedded HII forms, strings and variable stores directly and
writes requested changes to the `AmdFwConfig` EFI variable for firmware to
apply on the next boot.

Settings expose their HII form path as the BIOS setting parent so interactive
clients can group related settings.
