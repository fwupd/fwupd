# PCI PSP

## Introduction

This plugin checks all information reported from the AMD Platform Security processor into
the operating system on select SOCs.

The lack of these sysfs files does *NOT* indicate the lack of these security features, it only
indicates the lack of the ability to export it to the operating system.

The availability of the sysfs files indicates that the PSP supports exporting this information
into the operating system.

## External Interface Access

This plugin requires read only access to attributes located within `/sys/bus/pci/devices/`.
