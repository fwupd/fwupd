---
title: Plugin: AMD Firmware Configuration
---

## Introduction

This plugin exposes BIOS settings described by AMD's `AmdFwCfg` ACPI tables.
It parses the embedded HII forms, strings and variable stores directly and
writes requested changes to the `AmdFwConfig` EFI variable for firmware to
apply on the next boot.

Setting names use `|` between HII form groups so interactive clients can
present the settings as a hierarchy without treating periods in firmware text
as separators.
