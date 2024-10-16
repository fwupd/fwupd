---
title: Plugin: Raspberry Pi Pico Microcontrollers
---

## Introduction

Raspberry Pi Pico Microcontrollers can expose a so-called "Reset" USB interface.
When this is exposed, they can be reset into the bootrom and then updated (using the UF2 plugin).

## GUID Generation

Raspberry Pi offers vendors to use the Raspberry Pi USB PID space.
However, each vendor can chose their own VID/PID instead.

Devices from MNT Research use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1209&PID_6D07`
* `USB\VID_1209&PID_6D07&REV_0001`

Note that the VID is the shared <https://pid.codes/1209/> VID.

## Update Behavior

The runtime interface should use a [DS-20 BOS descriptor](https://fwupd.github.io/libfwupdplugin/ds20.html)
with something like these as the contents:

    Plugin=rp_pico
    Flags=internal
    Version=1.2.3
    VersionFormat=triplet
    Icon=computer

Devices are updated by triggering a reset, and then entering the Raspberry Pi Pico bootrom.

The bootrom speaks the UF2 protocol.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.2`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Lukas F. Hartmann: @mntmn
