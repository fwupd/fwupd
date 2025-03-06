---
title: Plugin: UEFI MOK
---

## Introduction

Shim 16.0 and newer export a `/sys/firmware/efi/mok-variables/HSIStatus` file that contains some
BootService-only attributes in an easy-to-digest format.

We can use these attributes to populate the `org.fwupd.hsi.UefiMemoryProtections` Host Security ID
attribute. The attributes are x64, aarch64 and riscv specific:

### shim-has-nx-compat-set

This attribute will be `1` if shim has the NX-compatible bit set in the COFF header,
and `0` otherwise.

### heap-is-executable

This attribute will be `1` if heap is executable, and `0` otherwise.

### stack-is-executable

This attribute will be `1` if the stack is executable, and `0` otherwise.

### ro-sections-are-writable

This attribute will be `1` if read-only sections are actually writable, and`0` otherwise.

### has-memory-attribute-protocol

This attribute will be `1` if the memory attribute protocol is supported by the firmware,
and `0` otherwise.

### has-dxe-services-table

This attribute will be `1` if the firmware provides a DXE services table, and `0` otherwise.

### has-get-memory-space-descriptor

This attribute will be `1` if `DxeServicesTable` has `GetMemorySpaceDescriptor()` populated,
and `0` otherwise.

### has-set-memory-space-attributes

This attribute will be `1` if `DxeServicesTable` has `SetMemorySpaceAttributes()` populated,
and `0` otherwise.

## External Interface Access

This plugin requires read access to `/sys/firmware/efi/mok-variables`.

## Version Considerations

This plugin has been available since fwupd version `2.0.7`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Richard Hughes: @hughsie
