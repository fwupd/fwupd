# Intel ME

## Introduction

This plugin is used to talk to the Intel ME device, typically CSME.

It allows us to get the Platform Key as used for BootGuard.

## GUID Generation

These devices use the existing GUIDs provided by the ME host interfaces.

## Vendor ID Security

The devices are not upgradable and thus require no vendor ID set.

## External Interface Access

This plugin requires `ioctl(IOCTL_MEI_CONNECT_CLIENT)` to `/dev/mei0`.
