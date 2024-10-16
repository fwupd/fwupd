---
title: Plugin: MNT Research
---

## Introduction

MNT Research devices are usually based on Raspberry Pi Pico chips.
They use a slightly modified reset protocol, and then boot into the normal Rasperry Pi Pico bootrom.

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1209&PID_6D07`

Note that the VID is the shared https://pid.codes/1209/ VID.

## Update Behavior

The device is updated by triggering a reset, and then entering the Raspberry Pi Pico bootrom.

The bootrom speaks the UF2 protocol.

## Version Considerations

This plugin has been available since fwupd version `2.0.SET_VERSION_HERE`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Lukas F. Hartmann: @mntmn
