---
title: Plugin: Aspeed AST2X00
---

## Introduction

This plugin checks the BMC has been set up correctly by checking configuration registers.

Vendors wanting to expose the AST2X00 functionality will need to add a Device Tree `compatible`
quirk entry to `aspeed-ast2x00.quirk`.

The current plugin will only work when run on the BMC itself (e.g. part of OpenBMC) but future
versions may report the same attributes when running on the host OS.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### AspeedAst2x00Revision

Revision number, e.g. `0x4` for AST2400, `0x5` for AST2500, etc.

Since: 1.8.10

## External Interface Access

This plugin requires access to raw system memory via `/dev/mem` when running on the SoC.
