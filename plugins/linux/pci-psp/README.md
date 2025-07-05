---
title: Plugin: PCI PSP — Platform Secure Processor
---

## Introduction

This plugin checks all information reported from the AMD Platform Secure processor into
the operating system on select SOCs.

The lack of these sysfs files does *NOT* indicate the lack of these security features, it only
indicates the lack of the ability to export it to the operating system.

The availability of the sysfs files indicates that the PSP supports exporting this information
into the operating system.

## External Interface Access

This plugin requires read only access to attributes located within `/sys/bus/pci/devices/`.

## Version Considerations

This plugin has been available since fwupd version `1.8.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Mario Limonciello: @superm1
